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

struct SaveOptions;

// Bit depth, dither and quality for Save As; the fields shown depend on the format.
class SaveOptionsDialog final : public QDialog
{
    Q_OBJECT

public:
    SaveOptionsDialog (const QString& extension, SaveOptions&, QWidget* parent);

private:
    void store();

    SaveOptions& options;
    QComboBox* bitDepth = nullptr;
    class QCheckBox* dither = nullptr;
    class QSpinBox* quality = nullptr;
};

// Normalize to a peak level or an integrated loudness target.
class NormalizeDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { peak, loudness };

    explicit NormalizeDialog (QWidget* parent);

    Mode mode() const;
    double target() const;

private:
    QComboBox* modeBox;
    class QDoubleSpinBox* targetBox;
};

// Personal colours for the waveform display, on top of the theme. Changes show
// immediately; Cancel or Esc puts the previous colours back.
class WaveformColorsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit WaveformColorsDialog (QWidget* parent);

    void done (int result) override;

private:
    void refresh();

    QMap<QString, QColor> original;
    QList<QPair<QString, class QPushButton*>> swatches;
};

// Choose a colour theme. Moving through the list previews it; Esc restores the old one.
class ThemeDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeDialog (QWidget* parent);

    void done (int result) override;

private:
    void reload();
    void preview();

    class QListWidget* list;
    QLabel* info;
    QString originalName;
};
