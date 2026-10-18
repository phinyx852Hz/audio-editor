// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"
#include "../core/Dsp.h"

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
    std::map<std::string, std::string> codecValues;   // MP3, M4A, Opus, WavPack...: the encoder's settings
};

// Normalize to a peak level or an integrated loudness target. Loudness has
// presets for streaming services and broadcast, can be matched to a reference
// file, and can limit the true peak so turning up does not clip. The choices
// are kept between uses.
class NormalizeDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { peak, loudness };

    explicit NormalizeDialog (QWidget* parent);

    Mode mode() const;
    double target() const;
    bool limitPeaks() const;         // loudness only
    double peakCeiling() const;      // dBTP
    QString targetName() const;      // for the undo history, e.g. "-14.0 LUFS (Spotify...)"

private:
    void matchReference();
    void store();

    QComboBox* modeBox;
    QComboBox* presetBox;
    class QDoubleSpinBox* targetBox;
    class QCheckBox* limitBox;
    class QDoubleSpinBox* ceilingBox;
    class QPushButton* referenceButton;
    class QLabel* referenceLabel;
    QString referenceName;
};

// Settings for Stereoize (mono to stereo). Values are kept between uses.
class StereoizeDialog final : public QDialog
{
    Q_OBJECT

public:
    StereoizeDialog (const dsp::StereoizeSettings& initial, bool wholeFile, int fileChannels, QWidget* parent);
    dsp::StereoizeSettings settings() const;

private:
    class QSpinBox* widthBox;
    class QDoubleSpinBox* delayBox;
    class QSpinBox* bassBox;
};

// The frequency band of a spectral selection, typed in (0 to 0 = every frequency).
class FrequencyRangeDialog final : public QDialog
{
    Q_OBJECT

public:
    FrequencyRangeDialog (double lowHz, double highHz, double nyquist, QWidget* parent);
    double lowHz() const;
    double highHz() const;

private:
    class QSpinBox* lowBox;
    class QSpinBox* highBox;
};

// Turn a spectral selection down or up; at the bottom of the range it is removed.
class SpectralGainDialog final : public QDialog
{
    Q_OBJECT

public:
    SpectralGainDialog (double initialDb, const QString& what, QWidget* parent);
    double gainDb() const;

private:
    class QDoubleSpinBox* gainBox;
};

// A small form of numbers and choices for a process, e.g. DeClick or DeHum.
// Each value is remembered under its settings key between uses.
class ValuesDialog final : public QDialog
{
    Q_OBJECT

public:
    struct Field
    {
        QString key;             // settings key, e.g. "repair/declick/sensitivity"
        QString label;
        double defaultValue = 0.0;
        double minimum = 0.0, maximum = 100.0, step = 1.0;
        int decimals = 0;
        QString suffix;
        QString tip;
        QStringList choices;     // when set: a list to pick from, and the value is the index
    };

    ValuesDialog (const QString& title, const QString& about, QList<Field> fields, QWidget* parent);
    double value (int index) const;
    void accept() override;      // also remembers the values

private:
    QList<Field> fields;
    QList<QWidget*> editors;
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
