// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QDialog>

class AudioEngine;
class CommandRegistry;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

// Searchable list of every command with its shortcut; Enter runs the selection.
// Doubles as the keyboard shortcut reference (F1).
class CommandPalette final : public QDialog
{
    Q_OBJECT

public:
    CommandPalette (CommandRegistry&, QWidget* parent);

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    void refilter();
    void runSelected();

    CommandRegistry& commands;
    QLineEdit* search;
    QTreeWidget* list;
};

// View and change every key binding, with conflict detection.
class KeymapDialog final : public QDialog
{
    Q_OBJECT

public:
    KeymapDialog (CommandRegistry&, QWidget* parent);

private:
    void rebuild();
    void editSelected();
    void clearSelected();

    CommandRegistry& commands;
    QTreeWidget* tree;
};

class AudioSettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    AudioSettingsDialog (AudioEngine&, QWidget* parent);

private:
    void apply();

    AudioEngine& engine;
    QComboBox* outputs;
    QLabel* status;
};
