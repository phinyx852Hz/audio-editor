// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Fft.h"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>
#include <deque>
#include <memory>
#include <vector>

class QComboBox;
class MilkDrop;

// The Visualizer: draws what PHINYX plays (after the Master Section), in one of
// several views, artistic or scientific. MainWindow feeds it the scope samples
// and calls tick() on its timer. Keys: Left/Right (or Page Up/Down) change the
// view; the window adds F11 for full screen and Esc to leave it.
class VisualizerView final : public QWidget
{
    Q_OBJECT

public:
    struct Mode
    {
        const char* id;
        const char* name;
        bool science;
        const char* about;
    };
    static const std::vector<Mode>& modes();
    static int modeIndex (const QString& id);   // -1 when unknown

    // `backdrop`: drawn behind the PHINYX window, so it takes no mouse or keys.
    VisualizerView (QWidget* parent, bool backdrop);
    ~VisualizerView() override;

    void setMode (int index);
    int mode() const { return current; }

    void addSamples (const std::vector<float>& left, const std::vector<float>& right, double sampleRate);
    void tick();   // analyse what came in and repaint

    // The MilkDrop view (projectM), made the first time it is shown.
    bool isMilkDrop() const;
    MilkDrop* milkDrop();
    void milkDropStep (int direction);
    void milkDropChoose (int index);
    void reloadMilkDrop();

Q_SIGNALS:
    void stepMode (int direction);
    void fullScreenRequested();
    void escapePressed();

protected:
    void paintEvent (QPaintEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void resizeEvent (QResizeEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;   // a backdrop follows its window's size

private:
    void analyse();
    void drawBars (QPainter&, bool radial);
    void drawCircleWave (QPainter&);
    void drawParticles (QPainter&);
    void drawRibbons (QPainter&);
    void drawSpectrum (QPainter&);
    void drawOctaves (QPainter&);
    void drawScope (QPainter&);
    void drawSpectrogram (QPainter&);
    void drawWaterfall (QPainter&);
    void drawSurface (QPainter&, bool science);
    void drawBackground (QPainter&);
    void drawTitle (QPainter&);

    float xForFrequency (double hz, double left, double width) const;
    double binFrequency (int bin) const { return bin * rate / analyzer.size(); }
    float levelAt (double hz) const;   // smoothed dB at a frequency

    bool backdrop;
    int current = 0;
    double rate = 48000.0;

    // The last samples, as rings.
    std::vector<float> ringLeft, ringRight;
    size_t ringPos = 0;
    qint64 lastSamplesAt = 0;
    QElapsedTimer clock;
    qint64 lastTick = 0;

    SpectrumAnalyzer analyzer { 12 };   // 4096 points
    std::vector<float> mono, spectrum, smooth, peak;   // dB per bin
    std::vector<qint64> peakAt;
    std::vector<float> bars, barPeaks;                 // 0 to 1, log-spaced
    std::vector<float> octaves, octavePeaks;           // dB, third-octave bands
    float level = 0.0f, bass = 0.0f, centroid = 0.0f; // 0 to 1
    double phase = 0.0;                                // slow rotation for the artistic views

    QImage spectrogram;   // one column per tick, log frequency up
    int spectrogramColumn = 0;
    std::deque<std::vector<float>> history;   // recent spectra for the waterfall, 0 to 1
    std::deque<std::vector<float>> surface;   // coarser and longer, for the 3D views, 0 to 1
    int surfaceTicks = 0;
    double cameraYaw = -32.0, cameraPitch = 28.0, cameraZoom = 1.0;   // the 3D views' camera (drag and wheel)
    QPointF dragFrom;
    QImage trail;   // the circle wave's afterglow

    std::unique_ptr<MilkDrop> milk;
    QImage milkFrame;

    struct Star { float x, y, z; };
    std::vector<Star> stars;

    qint64 modeShownAt = -100000;   // when the view's name was last shown
};

// The Visualizer window (Shift+F11), which can go full screen (F11). Floating by default.
class VisualizerWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit VisualizerWindow (QWidget* parent);

    VisualizerView* view() const { return visual; }
    void syncPicker();
    void toggleFullScreen();
    bool closeWhenLeavingFullScreen = false;   // opened straight into full screen: Esc closes it

Q_SIGNALS:
    void modeChosen (int index);
    void backdropRequested();
    void closed();

protected:
    void closeEvent (QCloseEvent*) override;

private:
    VisualizerView* visual;
    QWidget* bar;
    QComboBox* picker;
};
