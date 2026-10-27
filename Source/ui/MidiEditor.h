// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The MIDI editor (owner, 2026-10-26): a mixer MIDI clip's notes on a piano roll whose rows are the
// pitch system the music is in. It opens as an ordinary 12-TET piano roll; pick a mode, raga,
// maqam, laras or a Scala .scl/.kbm and the rows become that system's degrees, named as its
// tradition names them (sargam, kepatihan numbers, wǔshēng, quarter-tone letters), and the notes
// play in its tuning. Edited as in Cubase's Key Editor: tools to pick, draw, erase, split and glue,
// snap and quantise, a velocity or controller lane, an info line for the picked notes, and step
// input from the MIDI keyboard or the computer's. Restyle, after MIDIRestyle, moves a melody into
// another system degree by degree, the notes it had kept as ghosts behind. Saved as plain keys,
// MPE or pitch bend by channel (NoteEditor.h).
#include "../mixer/NoteEditor.h"

#include <QDialog>

#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTimer;
class QToolButton;

namespace phi::scala { struct Scale; struct KeyboardMap; }

class MidiEditor final : public QDialog
{
    Q_OBJECT

public:
    struct Host
    {
        // Plays a message on the clip's track's instrument (auditions, playback, the keyboard's notes).
        std::function<void (uint8_t status, uint8_t data1, uint8_t data2)> play;
        int system = -1;     // the tuning in force on the clip (Tunings.h), -1 12-TET
        int root = 0;        // its root, 0 (C) .. 11 (B)
        double bpm = 0.0;    // the mixer's tempo when the clip follows it, else 0 (the file's)
        QString folder;      // where to look for .scl and .kbm files
        QString title;       // the clip's name
    };

    MidiEditor (const midifile::Song&, Host, QWidget* parent);
    ~MidiEditor() override;

    // The notes as edited, encoded as chosen (after accept()).
    midifile::SongPtr result() const;
    QString summary() const;   // how it was saved, for the status line
    // A message from the MIDI keyboard, on the UI thread: step input when it is on.
    void midiIn (uint8_t status, uint8_t data1, uint8_t data2);

    class Roll;
    class Spectrum;
    class Wheel;

protected:
    void keyPressEvent (QKeyEvent*) override;
    void done (int) override;

private:
    friend class Roll;
    friend class Spectrum;
    friend class Wheel;

    void buildSystems();
    void systemChosen();
    void loadScala();
    void loadKeyboardMap();
    void exportScala();
    void setSystem (noteedit::PitchSystem);
    // Tuning changes within the clip: the rows show the system in force at the step cursor.
    const noteedit::PitchSystem& systemAt (int64_t tick) const;
    void showSystemAt (int64_t tick);
    void selectSystemItem (const noteedit::PitchSystem&);   // the System and root boxes, quietly
    void changeSystemHere();
    void removeSystemHere();
    // An edit on the picked notes, each stretch of the clip in its own system.
    void editBySystem (const std::function<void (noteedit::Sequence&, const noteedit::PitchSystem&)>&);
    void edit (const std::function<void (noteedit::Sequence&)>&);
    void undo();
    void redo();
    void changed();          // the sequence changed: the roll, the info line, the encoding's note
    void viewsUpdate();      // the spectrum and the wheel, after a change or as the playhead moves
    int64_t nowTick() const; // what the wheel shows: the playhead, else the step cursor
    void updateInfo();
    void infoEdited();
    void restyleDialog();
    void velocityDialog();
    void togglePlay();
    void startPlay (bool original);
    void stopPlay();
    void playTick();
    void audition (double pitch, int velocity = 100);
    void stepPitch (double pitch, bool chord);
    int64_t grid() const;
    int64_t noteLength() const;
    noteedit::EncodeOptions encodeOptions() const;
    double pitchForKey (int key) const;   // what a MIDI key enters in the system shown

    Host host;
    midifile::Song original;
    noteedit::Sequence seq, ghosts;
    std::vector<noteedit::Sequence> undoList, redoList;
    noteedit::PitchSystem system;   // the one shown: in force at the step cursor
    noteedit::PitchSystem base;     // the clip's, before its first tuning change
    std::vector<noteedit::PitchSystem> otherSystems;   // read from the clip, in the System box
    std::unique_ptr<phi::scala::Scale> scala;
    std::unique_ptr<phi::scala::KeyboardMap> keyboardMap;
    QString scalaName;
    noteedit::StepInput step;
    int heldKeys = 0;
    int64_t chordAt = -1;

    Roll* roll = nullptr;
    Spectrum* spectrum = nullptr;
    Wheel* wheel = nullptr;
    QComboBox* viewBox = nullptr;
    QCheckBox* wheelBox = nullptr;
    QSpinBox* harmonicsBox = nullptr;
    QComboBox* systemBox = nullptr;
    QComboBox* rootBox = nullptr;
    QComboBox* namingBox = nullptr;
    QComboBox* rowsBox = nullptr;
    QComboBox* gridBox = nullptr;
    QComboBox* lengthBox = nullptr;
    QComboBox* encodingBox = nullptr;
    QDoubleSpinBox* bendRangeBox = nullptr;
    QDoubleSpinBox* strengthBox = nullptr;
    QDoubleSpinBox* swingBox = nullptr;
    QCheckBox* snapBox = nullptr;
    QCheckBox* ghostBox = nullptr;
    QCheckBox* stepBox = nullptr;
    QCheckBox* chordBox = nullptr;
    QCheckBox* insertBox = nullptr;
    QSpinBox* octaveBox = nullptr;
    QComboBox* laneBox = nullptr;
    QSpinBox* laneChannelBox = nullptr;
    QToolButton* playButton = nullptr;
    QToolButton* abButton = nullptr;
    std::vector<QToolButton*> toolButtons;
    QLabel* infoLabel = nullptr;
    QSpinBox* infoVelocity = nullptr;
    QDoubleSpinBox* infoPitch = nullptr;
    QSpinBox* infoLength = nullptr;
    QLabel* encodingNote = nullptr;
    bool updatingInfo = false;
    bool encodingChosen = false;   // picked by hand: not changed for the notes
    size_t infoMark = (size_t) -1;   // the undo list's size after the last typed change

    // Playback in the editor: the encoded notes sent at their times.
    QTimer* playTimer = nullptr;
    struct Timed { double seconds; uint8_t status, data1, data2; };
    std::vector<Timed> playing;
    size_t playAt = 0;
    double playFrom = 0.0;      // seconds: where the play started
    qint64 playStarted = 0;     // ms, the clock when it started
    bool playingOriginal = false;
    int64_t playheadTick = -1;
    midifile::SongPtr playSong;
};
