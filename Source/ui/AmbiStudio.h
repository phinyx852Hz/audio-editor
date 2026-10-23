// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/SpatialStudio.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>

class AmbiScene;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

// The heads-up display: the room from above (front up) and from the side, with every
// sound the rack places; the chosen device's sounds bright. Drag to move a panner.
class StudioHud final : public QWidget
{
    Q_OBJECT

public:
    explicit StudioHud (QWidget* parent);

    // All devices' dots; `selected` is drawn bright and, when `draggable`, moves with the mouse.
    void setDots (const std::vector<std::vector<studio::Dot>>& dots, int selected, bool draggable);
    QSize sizeHint() const override { return { 460, 230 }; }

Q_SIGNALS:
    void moved (double azimuth, double elevation, double radius, bool top);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;

private:
    QRectF topArea() const;
    QRectF sideArea() const;
    void dragTo (QPointF, bool top);

    std::vector<std::vector<studio::Dot>> all;
    int selected = -1;
    bool draggable = false;
    int dragging = 0;
};

// The level of each channel of the scene (W, Y, Z, X...), as E4L's bus meters.
class StudioMeters final : public QWidget
{
public:
    explicit StudioMeters (QWidget* parent);
    void setLevels (const std::vector<float>& dbs);   // empty: clear
    QSize sizeHint() const override { return { 240, 110 }; }

protected:
    void paintEvent (class QPaintEvent*) override;

private:
    std::vector<float> levels;
};

// Spatial > Ambisonic Studio: E4L's devices in one floating panel. A rack of panners
// and effects on the left, the chosen device's controls and heads-up display in the
// middle, and the master bus on the right (meters, scope, decoder, preview, apply).
class AmbiStudio final : public QDialog
{
    Q_OBJECT

public:
    struct Input
    {
        AudioDataPtr data;
        int inputOrder = 0;     // > 0: the file is ambisonic (AmbiX)
        Frame start = 0, end = 0;   // what the preview plays
        double bpm = 0.0;       // the file's tempo, 0 when it has none
    };

    struct Host
    {
        std::function<bool (Input&)> input;                                  // false: no file
        std::function<void (AudioDataPtr stereo, Frame start, Frame end)> preview;   // null stops it
        std::function<double()> previewSeconds;                              // the playhead from the preview's start, < 0 when not heard
        std::function<void (const studio::Scene&)> apply;
        studio::Loader load;
        QStringList hrtfFiles;
    };

    AmbiStudio (Host host, QWidget* parent);
    ~AmbiStudio() override;

    // The file changed (another file opened, an edit): the preview is made again if it is on.
    void inputChanged();

protected:
    void closeEvent (class QCloseEvent*) override;
    void keyPressEvent (class QKeyEvent*) override;

private:
    void buildRack (QWidget* parent, QVBoxLayout* into);
    void buildMaster (QWidget* parent, QVBoxLayout* into);
    void refreshRack();
    void showDevice (int index);
    void addDevice (studio::Kind);
    void loadPreset (const studio::Scene&);
    void changed (bool rebuildEditor = false);
    void save() const;
    void startPreview();
    void stopPreview();
    void tick();
    QString decoderName() const;
    void fillCustomDecoders();

    Host host;
    studio::Scene scene;
    int current = -1;
    int shownIndex = -1;   // the device the editor shows, to keep its scroll position

    QListWidget* rack;
    QScrollArea* editorScroll;
    QLabel* deviceTitle;
    QLabel* deviceText;
    StudioHud* hud;
    StudioMeters* meters;
    AmbiScene* scope;
    QComboBox* orderBox;
    QDoubleSpinBox* bpmBox;
    QDoubleSpinBox* tailBox;
    QComboBox* decoderBox;
    QComboBox* hrtfBox;
    QComboBox* monitorBox;
    QDoubleSpinBox* masterGainBox;
    QCheckBox* previewBox;
    QPushButton* applyButton;
    QLabel* status;
    bool updating = false;
    std::map<std::string, std::function<void (double)>> setters;   // the shown device's number controls, by parameter

    // The preview: rendered on another thread, newest wins.
    struct Preview
    {
        studio::Result result;
        AudioDataPtr heard;
        Frame start = 0, end = 0;
        std::string error;
    };
    QFutureWatcher<std::shared_ptr<Preview>> watcher;
    std::shared_ptr<Preview> preview;
    std::shared_ptr<std::atomic<bool>> cancelRender;
    bool renderAgain = false;
    QTimer previewTimer, frameTimer;
    std::string idleJson;   // the rack the idle display was worked out for
    std::vector<std::vector<studio::Dot>> idleDots;
};
