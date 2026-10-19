// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QWidget>

class TrackerEngine;
class QLabel;
class QListWidget;
class QPushButton;
class QCheckBox;

// The pattern of the order position being looked at, laid out like MilkyTracker
// and FastTracker 2: a row per line, each channel as note, instrument, volume
// and effect. A header over each channel shows its number, mute (M), solo (S)
// and the mixer track it records to (T1 to T8).
class PatternView final : public QWidget
{
    Q_OBJECT

public:
    explicit PatternView (TrackerEngine&, QWidget* parent);

    void setOrder (int order);   // shows that order position's pattern
    int order() const { return currentOrder; }
    int row() const { return cursorRow; }
    int channel() const { return cursorChannel; }
    void setRow (int row);
    void setPlayRow (int row);   // the row being heard, or -1
    void moduleChanged();

    QSize sizeHint() const override { return { 820, 520 }; }

Q_SIGNALS:
    void orderStep (int direction);   // Ctrl+Left/Right, or past the pattern's ends
    void channelsChanged();           // mute, solo or track changed

protected:
    void paintEvent (QPaintEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void focusInEvent (QFocusEvent*) override { update(); }
    void focusOutEvent (QFocusEvent*) override { update(); }

private:
    int rows() const;
    int columnWidth() const;
    int rowNumberWidth() const;
    void keepChannelVisible();
    void announce();

    TrackerEngine& engine;
    int currentOrder = 0, cursorRow = 0, cursorChannel = 0, firstChannel = 0, playRow = -1;
    static constexpr int headerHeight = 44;
};

// The Tracker window (Tracker menu): MilkyTracker's module player in PHINYX. It opens
// and plays modules, follows the pattern being played, and records the song into a
// new editor tab, stereo or one stereo pair per mixer track.
class TrackerWindow final : public QWidget
{
    Q_OBJECT

public:
    TrackerWindow (TrackerEngine&, QWidget* parent);

    PatternView* patternView() const { return pattern; }
    void moduleChanged();
    void refresh();   // from the UI timer: follows playback
    bool following() const;
    void setFollowing (bool);

Q_SIGNALS:
    void openRequested();
    void playSongRequested();
    void playPatternRequested();
    void stopRequested();
    void recordRequested (bool multiChannel);
    void closed();

protected:
    void closeEvent (QCloseEvent*) override;
    void keyPressEvent (QKeyEvent*) override;

private:
    void showOrder (int order);

    TrackerEngine& engine;
    QLabel* title;
    QLabel* status;
    QListWidget* orders;
    QListWidget* instruments;
    PatternView* pattern;
    QCheckBox* follow;
    QPushButton* playSong;
    QPushButton* stop;
    int shownPlayOrder = -1;
};
