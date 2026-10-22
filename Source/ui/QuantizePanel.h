// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/BeatGrid.h"
#include "../core/Quantize.h"

#include <QWidget>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

// One bar of the quantize grid: the lines hits are pulled to (swung or grooved), the
// catch range around each (shaded) with the non-quantize zone at its centre, and the
// hits of the range folded into one bar, so you see the feel before you apply it.
class QuantizeGridView final : public QWidget
{
    Q_OBJECT

public:
    explicit QuantizeGridView (QWidget* parent);
    void setGrid (int beatsPerBar, const audioquantize::Settings&);
    void setHits (std::vector<double> beatsInBar) { hits = std::move (beatsInBar); update(); }
    QSize sizeHint() const override { return { 320, 56 }; }
    QSize minimumSizeHint() const override { return { 200, 48 }; }

protected:
    void paintEvent (class QPaintEvent*) override;

private:
    int beatsPerBar = 4;
    audioquantize::Settings settings;
    std::vector<double> hits;
};

// The Quantize panel (Warp > Quantize Panel, Shift+Q): hitpoints, the quantize grid
// with strength, swing, grooves, catch range, non-quantize zone and randomize, and
// the warp algorithm, in one place that docks or pops out. It only shows and asks;
// MainWindow does the work (QuantizeSection.cpp). Its settings are remembered.
class QuantizePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit QuantizePanel (QWidget* parent);

    audioquantize::Detection detection() const;
    double hitFilter() const;                   // beats between kept hits' lines; 0 keeps every hit
    audioquantize::Settings settings() const;   // seed included
    double snapDivision() const;                // where dragged warp points snap: the grid step, 0 when free
    bool autoApply() const;
    bool iterative() const;
    int mode() const;
    void setMode (int);

    // What the panel shows of the file in front.
    struct State
    {
        bool open = false;
        bool editing = false;          // warp markers shown and editable
        BeatGrid grid;
        int hits = 0, markers = 0;
        QString range;                 // "the selection (1.2 s)" or "the whole file"
        std::vector<double> hitsInBar; // where the range's hits fall in a bar, in beats
    };
    void setState (const State&);
    void setStatus (const QString&);
    void addGroove (const audioquantize::Groove&);   // remembered and chosen
    QWidget* focusTarget() const;

Q_SIGNALS:
    void editToggled (bool);
    void detectionChanged();          // method, threshold, gap or filter moved: find the hits again
    void settingsChanged();           // a quantize setting moved (Auto Apply quantizes again)
    void quantizeRequested();
    void resetRequested();            // the warp markers in the range removed: the audio as recorded
    void markersFromHitsRequested();  // a warp marker on every hit, where it is
    void sliceRequested();            // a slice marker at every hit
    void grooveFromHitsRequested();
    void modeChanged (int);
    void snapChanged();
    void stretchRequested (double speed);   // 2: half as long, 0.5: twice as long
    void stretchToTempoRequested();
    void detectTempoRequested();
    void setTempoRequested();

private:
    void load();
    void save() const;
    void fillGrooves();
    void updateEnabled();
    void showGrid();

    QPushButton* edit;
    QComboBox* method;
    QSlider* threshold;
    QSpinBox* gap;
    QComboBox* filter;
    QLabel* hitCount;

    QComboBox* division;
    QComboBox* tuplet;
    QComboBox* groove;
    QSlider* strength;
    QSlider* swing;
    QSlider* catchRange;
    QSlider* nonQ;
    QDoubleSpinBox* randomMs;
    QPushButton* reseed;
    QCheckBox* iterate;
    QCheckBox* autoBox;
    QuantizeGridView* gridView;
    QLabel* rangeLabel;
    QPushButton* quantize;

    QComboBox* algorithm;
    QLabel* algorithmHelp;
    QCheckBox* snap;
    QLabel* tempo;

    QLabel* status;
    unsigned seed = 1;
    bool updating = false;
    State state;
};
