// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/ThemeGen.h"
#include "Theme.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QTableWidget;
class QTimer;
namespace themeai { class Client; struct Suggestion; }

// Tools > Theme Generator: builds a whole theme from a colour, a picture, an existing
// theme or a REAPER colour theme, and optionally from a description (themeai). The
// whole app previews it while you work; Save puts it in the themes folder, where
// Options > Theme finds it. See docs/themes.md.
class ThemeGeneratorDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeGeneratorDialog (QWidget* parent);
    ~ThemeGeneratorDialog() override;

    void done (int result) override;

private:
    void regenerate();          // from settings and locked colours, then previews
    void previewSoon();
    void showSettings();        // controls from `settings`
    void showColors();
    void showContrast();
    void setSeed (const themegen::Rgb&);
    void startFromPicture (const QString& path);
    void startFromTheme (int index);
    void startFromReaper();
    void showPalette (const std::vector<themegen::Swatch>&);
    void editColor (int row);
    void describe();
    void suggested (const themeai::Suggestion&);
    void aiSettings();
    void updateAiControls();
    bool save();
    void exportFile();
    Theme theme() const;        // the result as a Theme, for previewing and checking
    QString json() const;

    themegen::Settings settings;
    themegen::Colors locked;
    themegen::Result result;
    Theme original;
    bool updating = false;

    QLineEdit* name;
    QPushButton* seedButton;
    QLineEdit* seedHex;
    QComboBox* fromTheme;
    QPushButton* fromWallpaper;
    QHBoxLayout* paletteRow;
    QLabel* paletteLabel;
    QComboBox* mode;
    QComboBox* harmony;
    QComboBox* meters;
    QSlider* saturation;
    QSlider* contrast;
    QSlider* tint;
    QCheckBox* autoFix;
    QTableWidget* colors;
    QListWidget* checks;
    class ThemePreview* preview;
    QLineEdit* description;
    QPushButton* describeButton;
    QLabel* aiStatus;
    themeai::Client* ai;
    QTimer* applyTimer;
};
