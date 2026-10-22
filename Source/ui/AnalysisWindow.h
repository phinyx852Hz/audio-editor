// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSplitter;

class AudioData;
class QLabel;

namespace analysis
{
    struct State;
    class Card;

    // The whole of a file measured at once, as a text report (loudness against the
    // target, amplitude statistics, stereo, RMS histogram, average spectrum).
    QString fileReport (const AudioData&, const QString& path, int targetIndex);
}

// Analysis Meters (Analyze > Analysis Meters Window, Ctrl+Alt+A): one window
// with everything there is to know about what PHINYX plays, as Adobe Audition's
// Analysis workspace has it, drawn more like a mastering meter suite:
// - Loudness (BS.1770 / EBU R128): momentary, short-term and integrated on a
//   gauge around a target (Streaming -14, Apple -16, EBU -23, ATSC -24, Club -9),
//   with the maximums, loudness range, true peak and PLR.
// - Loudness History: short-term and momentary over the last 30 s to 10 min,
//   coloured by how far they are from the target.
// - Levels: peak, RMS and true peak for left and right, with holds and clip counts.
// - Spectrum: like Audition's Frequency Analysis, with fractional-octave smoothing,
//   a slope, peak hold, eight hold traces (1-8) and the note under the mouse.
// - Spectrogram, Imaging (the Goniometer and its views), Phase Wheel (where each
//   frequency sits between mono, left, right and inverse), Correlation per band
//   with the overall correlation and balance, and Amplitude Statistics as in
//   Audition (Copy puts them on the clipboard), with an RMS histogram.
// Measured from what is heard, after the Master Section, since the last reset.
// Each meter is a card that can be hidden (Meters menu) and the splitters
// between them moved; both are remembered. Keys: Space plays or stops, R
// resets, F freezes, 1-8 take or clear a spectrum hold, F11 full screen, Esc closes.
// Drop audio files on it for a full report of each (AnalysisReport), measured from
// the file itself rather than from what plays.
class AnalysisWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisWindow (QWidget* parent);
    ~AnalysisWindow() override;

    void addSamples (const std::vector<float>& left, const std::vector<float>& right, double sampleRate);
    void tick();     // call on the UI timer, also when nothing plays, so the meters fall
    void reset();

Q_SIGNALS:
    void playStopRequested();
    void closed();

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void closeEvent (class QCloseEvent*) override;
    void dragEnterEvent (class QDragEnterEvent*) override;
    void dragLeaveEvent (class QDragLeaveEvent*) override;
    void dropEvent (class QDropEvent*) override;
    void resizeEvent (class QResizeEvent*) override;

private:
    void setFrozen (bool);
    void toggleHold (int slot);
    void saveLayout() const;
    void restoreLayout();
    void updateStatus();
    void analyseFile (const QString& path);
    void showMessage (const QString&);

    std::unique_ptr<analysis::State> state;
    std::vector<analysis::Card*> cards;
    std::vector<QSplitter*> splitters;
    QSplitter* rows = nullptr;
    QComboBox* targetBox = nullptr;
    QPushButton* freezeButton = nullptr;
    QLabel* status = nullptr;
    QMenu* metersMenu = nullptr;
    class Goniometer* imaging = nullptr;
    QWidget* spectrum = nullptr;
    std::vector<QPushButton*> holdButtons;
    bool frozen = false;
    int messageTicks = 0;
    QElapsedTimer clock;
    QLabel* dropHint = nullptr;
    int measuring = 0;   // files being read and measured for a report
};
