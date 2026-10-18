// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/Export.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QWidget;

// Export, like WaveLab's rendering: a list of outputs written in one go, each
// with its own format and settings, sample rate, bit depth, dither, loudness
// target, true-peak ceiling, name pattern, folder and metadata. The list is
// remembered and can be saved as presets. Preview hears an output's encoding
// against the original (A/B), or only what the codec removes.
class ExportDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class Range { whole, selection, regions, markers };

    struct Context
    {
        bool hasSelection = false, masterActive = false;
        int regions = 0, markers = 0;
        double sampleRate = 48000.0;
        int channels = 2;
        QString sourcePath;
        Tags tags;
    };

    ExportDialog (const Context&, QWidget* parent);

    std::vector<exporter::Output> outputs() const { return list; }
    Range range() const;
    bool throughMaster() const;

    static std::vector<exporter::Output> remembered();
    static void remember (const std::vector<exporter::Output>&);
    static exporter::Output defaultOutput (const std::string& formatId);

Q_SIGNALS:
    // Hear the current output's encoding (A/B with the original), or only what it removes.
    void previewRequested (const exporter::Output&, bool difference);
    void previewStopped();
    void abRequested();

private:
    void showOutput (int row);
    void readForm();
    void buildSettings();
    void refreshList();
    void refreshPath();
    void loadPreset();
    void savePreset();

    Context context;
    std::vector<exporter::Output> list;
    int shown = -1;
    bool filling = false;

    QListWidget* outputList;
    QLineEdit* nameEdit;
    QComboBox* formatBox;
    QFormLayout* settingsForm;
    int settingsRow = 0, settingRows = 0;   // where the format's settings are in the form, and how many
    QComboBox* rateBox;
    QComboBox* depthBox;
    QComboBox* ditherBox;
    QCheckBox* normalizeBox;
    QComboBox* loudnessPreset;
    QDoubleSpinBox* targetBox;
    QCheckBox* limitBox;
    QDoubleSpinBox* ceilingBox;
    QLineEdit* folderEdit;
    QLineEdit* patternEdit;
    QLabel* pathLabel;
    QComboBox* overwriteBox;
    QComboBox* templateBox;
    QCheckBox* fillLoudnessBox;
    QCheckBox* checkPeaksBox;
    QWidget* schemaArea;
    QLabel* noteLabel;
    QComboBox* rangeBox;
    QCheckBox* masterBox;
    QPushButton* previewButton;
    QCheckBox* differenceBox;
};
