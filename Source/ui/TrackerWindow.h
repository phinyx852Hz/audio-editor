// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../tracker/TrackerEngine.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;

// The pattern of the order position being looked at, laid out like MilkyTracker
// and FastTracker 2: a row per line, each channel as note, instrument, volume
// and effect. A header over each channel shows its number, mute (M), solo (S)
// and the mixer track it records to (T1 to T8).
//
// Editing works as in FastTracker 2: in edit mode the keyboard is a piano by
// position (Z to M and Q to P for two octaves, the keys above for the sharps),
// F1 to F8 pick the octave, the other columns take hex digits, Delete clears,
// Insert and Backspace push and pull the rows below.
class PatternView final : public QWidget
{
    Q_OBJECT

public:
    explicit PatternView (TrackerEngine&, QWidget* parent);

    void setOrder (int order);   // shows that order position's pattern
    int order() const { return currentOrder; }
    int row() const { return cursorRow; }
    int channel() const { return cursorChannel; }
    int column() const { return cursorColumn; }
    void setRow (int row);
    void setPlayRow (int row);   // the row being heard, or -1
    void moduleChanged();

    bool isEditing() const { return editMode; }
    void setEditing (bool);
    int octave() const { return baseOctave; }
    void setOctave (int);
    int editStep() const { return step; }
    void setEditStep (int);
    int instrument() const { return currentInstrument; }
    void setInstrument (int);

    QSize sizeHint() const override { return { 820, 520 }; }

Q_SIGNALS:
    void orderStep (int direction);   // Ctrl+Left/Right, or past the pattern's ends
    void channelsChanged();           // mute, solo or track changed
    void edited();                    // the pattern or song changed
    void editingChanged (bool);
    void settingsChanged();           // octave, step or instrument
    void notePreview (int channel, int note, int instrument);
    void noteRelease (int channel, int instrument);
    void message (const QString&);

protected:
    bool event (QEvent*) override;
    void paintEvent (QPaintEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    void keyReleaseEvent (QKeyEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void focusInEvent (QFocusEvent*) override { update(); }
    void focusOutEvent (QFocusEvent*) override { update(); }

private:
    int rows() const;
    int patternIndex() const;
    int columnWidth() const;
    int rowNumberWidth() const;
    int lineHeight() const;
    int rowAt (int y) const;
    void keepChannelVisible();
    void announce();
    int noteForKey (const QKeyEvent*) const;   // by key position; keyOff, or -1
    bool typeDigit (const QKeyEvent*);
    void advance();
    bool hasSelection() const { return selecting; }
    void selectionBounds (int& row0, int& row1, int& channel0, int& channel1) const;
    void afterEdit (bool changed);

    TrackerEngine& engine;
    int currentOrder = 0, cursorRow = 0, cursorChannel = 0, cursorColumn = 0, firstChannel = 0, playRow = -1;
    bool editMode = false;
    int baseOctave = 4, step = 1, currentInstrument = 1;
    bool selecting = false;
    int anchorRow = 0, anchorChannel = 0;
    std::vector<int> heldKeys;   // scan codes of notes being previewed
    static TrackerEngine::Block clipboard;
    static constexpr int headerHeight = 44;
};

// The Tracker window (Tracker menu): MilkyTracker's player and editor in PHI. It
// opens, edits and saves modules, plays and follows them, and records the song into
// a new editor tab, stereo or one stereo pair per mixer track.
class TrackerWindow final : public QWidget
{
    Q_OBJECT

public:
    TrackerWindow (TrackerEngine&, QWidget* parent);

    PatternView* patternView() const { return pattern; }
    void moduleChanged();
    void songChanged();   // after an edit: the lists, title and settings
    void refresh();       // from the UI timer: follows playback
    bool following() const;
    void setFollowing (bool);
    int currentInstrument() const;

Q_SIGNALS:
    void newRequested();
    void openRequested();
    void saveRequested (bool saveAs);
    void loadSampleRequested (int instrument);
    void playSongRequested();
    void playPatternRequested();
    void stopRequested();
    void recordRequested (bool multiChannel);
    void linkRequested (int instrument, int slot);   // slot 0: the instrument's own sample
    void retroSamplerRequested();                    // show the Retro Sampler's window
    void notePreview (int channel, int note, int instrument);
    void noteRelease (int channel, int instrument);
    void closed();

protected:
    void closeEvent (QCloseEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    void showOrder (int order);
    void fillOrders();
    void fillInstruments();
    void syncSettings();
    bool ensureEditable();
    void orderInsert (bool newPattern);
    void orderDelete();
    void orderChangePattern (int by);
    void renameInstrument();
    void fillLinkMenu (class QMenu*);

    TrackerEngine& engine;
    QLabel* title;
    QLabel* status;
    QListWidget* orders;
    QListWidget* instruments;
    PatternView* pattern;
    QCheckBox* follow;
    QCheckBox* edit;
    QPushButton* playSong;
    QPushButton* stop;
    QLineEdit* songTitle;
    QSpinBox* bpm;
    QSpinBox* speed;
    QSpinBox* channels;
    QSpinBox* patternRows;
    QSpinBox* octave;
    QSpinBox* stepBox;
    int shownPlayOrder = -1;
    bool syncing = false;
};
