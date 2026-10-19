// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// MilkDrop presets through projectM, rendered off screen (its own OpenGL context
// and framebuffer) into an image the Visualizer paints like its other views, so
// it works in the window, full screen and behind PHINYX alike.
class MilkDrop
{
public:
    // Built with projectM? (The view is left out otherwise.)
    static bool builtIn();
    // Where presets are looked for: yours first, then the system's.
    static QStringList presetFolders();
    static QString userPresetFolder();   // ~/.local/share/phinyx/milkdrop
    static QString categoryOf (const QString& presetPath);   // its folders under the preset folder, or nothing

    MilkDrop();
    ~MilkDrop();

    bool ready() const;
    QString error() const;
    int presetCount() const;
    QString presetName() const;      // the one showing, without folders or .milk
    QString presetCategory() const;  // its folder, such as Fractal
    QStringList presets() const;     // every preset's path
    bool changed();                  // true once after the preset changed

    void addSamples (const float* left, const float* right, size_t count);
    QImage render (QSize size);

    void next();
    void previous();
    void choose (int index);
    int position() const;
    void setLocked (bool);
    bool locked() const;
    void reloadPresets();

private:
    struct State;
    std::unique_ptr<State> state;
};
