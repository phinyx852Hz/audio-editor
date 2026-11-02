// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include <vector>

class AudioEngine;
class QLabel;
class QPushButton;
class QTreeWidget;

// Chain presets for the Master Section (Master > Load Chain Preset, Save Chain Preset,
// and in Find): whole chains in .phchain files. The factory ones ship in
// share/master-presets, yours are kept in ~/.local/share/phi/master-presets.
namespace chainpresets
{
    QString factoryFolder();
    QString userFolder();

    struct Entry
    {
        QString path, name, description;
        QString group;          // "Factory", or a factory subfolder ("Factory: Loud"), or "Yours"
        QStringList plugins;    // in slot order
        bool factory = true;
    };
    std::vector<Entry> all();

    // Loads one, after asking when it would replace plugins already in the chain.
    // False when it was not loaded (cancelled, or the file is not a preset).
    bool load (AudioEngine&, QWidget* parent, const QString& path);
    // Asks for a name and a description and saves the chain as it is; the file, or empty.
    QString saveCurrent (AudioEngine&, QWidget* parent);
}

// The list of presets: factory ones by folder, then yours, with what each one holds.
// Enter or Load loads the chosen one; Delete removes one of yours.
class ChainPresetDialog final : public QDialog
{
public:
    ChainPresetDialog (AudioEngine&, QWidget* parent);
    bool loadedOne() const { return loaded; }

private:
    void fill();
    void showChosen();
    void loadChosen();
    void deleteChosen();

    AudioEngine& engine;
    QTreeWidget* list = nullptr;
    QLabel* about = nullptr;
    QPushButton* loadButton = nullptr;
    QPushButton* deleteButton = nullptr;
    bool loaded = false;
};
