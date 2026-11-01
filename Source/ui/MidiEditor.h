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
// MPE or pitch bend by channel (NoteEditor.h). The staff notation (MidiStaff.h) takes the roll's
// place at the toggle, the same notes written on a staff in Xen Tuner's notations or in cipher.
#include "../core/AudioData.h"
#include "../mixer/NoteEditor.h"

#include <QDialog>

#include <functional>
#include <map>
#include <memory>
#include <vector>

class AudioCapture;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QSpinBox;
class QTimer;
class QToolButton;
class TransportButton;

namespace phi::scala { struct Scale; struct KeyboardMap; }

class MidiEditor final : public QDialog
{
    Q_OBJECT

public:
    // The transport the editor's is tied to (owner, 2026-10-30: "Tied to the main transport"): the
    // main window's for its MIDI panel, the mixer's for a clip or a score. Play in either starts
    // both from one place, Pause or Stop in either stops both, and the editor's notes keep to its
    // clock. Times are the editor's seconds: 0 is the clip's first tick.
    struct Transport
    {
        std::function<bool()> available;          // it can play now (a file is open); else the editor plays alone
        std::function<bool()> playing;
        std::function<double()> seconds;          // where it is
        std::function<void (double seconds)> play;   // from there
        std::function<void()> stop;
        std::function<void (double seconds)> locate;  // moved there, playing on if it plays
    };

    struct Host
    {
        // Plays a message on the clip's track's instrument (auditions, playback, the keyboard's notes).
        std::function<void (uint8_t status, uint8_t data1, uint8_t data2)> play;
        int system = -1;     // the tuning in force on the clip (Tunings.h), -1 12-TET
        int root = 0;        // its root, 0 (C) .. 11 (B)
        double bpm = 0.0;    // the mixer's tempo when the clip follows it, else 0 (the file's)
        QString folder;      // where to look for .scl and .kbm files
        QString title;       // the clip's name
        QString inputSource; // the audio input to hum into (a PipeWire target); empty the first input
        // Reads a sound file (to hear its melody), null with the reason when it cannot.
        std::function<AudioDataPtr (const QString& path, QString& error)> loadAudio;
        // Opens on the staff notation instead of the piano roll (MidiStaff.h): a score from
        // Architectonics, say.
        bool staff = false;
        // Opens on this notation (notation::systems()' id: "phi" PHI's staff, "bhatkhande",
        // "jeongganbo"...); empty: as `staff` says.
        QString notation;
        // A panel of the main window rather than a dialog (MidiPanel.h): no Apply or Cancel, and
        // Escape or Enter never closes it.
        bool embedded = false;
        Transport transport;   // none: the editor plays on its own clock
    };

    MidiEditor (const midifile::Song&, Host, QWidget* parent);
    ~MidiEditor() override;

    // The notes as edited, encoded as chosen (after accept()).
    midifile::SongPtr result() const;
    QString summary() const;   // how it was saved, for the status line
    // A recording dropped on the editor (MidiPanel.h): its melody, pitch tracked as Hum or Sing
    // hears it, or its drum hits as kick, snare and hat on channel 10 (mixer/DrumsToMidi.h), put
    // in from the step cursor. False when nothing was heard.
    bool putInAudio (const AudioData&, bool drums);
    // Playback and recording stopped, every note let go (the panel closing).
    void stop() { setRecording (false); stopPlay(); }
    // A message from the MIDI keyboard, on the UI thread: step input when it is on.
    void midiIn (uint8_t status, uint8_t data1, uint8_t data2);

    class Roll;
    class Spectrum;
    class Staff;
    class Wheel;

protected:
    void keyPressEvent (QKeyEvent*) override;
    bool event (QEvent*) override;   // the editor's keys are its own, not the main window's
    void done (int) override;

private:
    friend class Roll;
    friend class Spectrum;
    friend class Staff;
    friend class Wheel;

    void buildSystems();
    void systemChosen();
    void loadScala();
    void loadKeyboardMap();
    void exportScala();
    // The piano roll (0), the rising spectrum (1) or the staff notation (2), in the same place.
    void showView (int);
    void fillNotationMenu (QMenu*);
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
    void humDialog();        // hum or sing to notes, or a recording's melody
    void putInHeard (const std::vector<noteedit::Heard>&, const noteedit::HumOptions&);
    double tempo() const;    // beats a minute: the mixer's, else the file's
    void goTo (int64_t tick, bool reveal = true);   // the step cursor and the playhead there (in view, scrolled to if `reveal`)
    void togglePlay();       // Play and Pause: on from the playhead, or held where it is
    void startPlay (bool original);
    void stopPlay (bool stopTied = true);   // and the transport it is tied to, unless false
    // Tied to the main transport: when it can play now, its clock, and following its starts,
    // stops and jumps.
    bool tied() const;
    double clockNow() const;  // seconds: the tied transport's, else the editor's own
    void follow();
    double secondsAt (int64_t tick) const;   // on the clip's tempo map
    int64_t tickAt (double seconds) const;
    void stopToStart();      // Stop: back to where Play was pressed; pressed again, to the start
    void playTick();
    void updateTransport();  // the Play symbol and the time
    // Record: the MIDI keyboard's notes go in at the playhead while it plays, a take one undo.
    void setRecording (bool);
    void closeHeld();        // the notes still held, ended at the playhead
    bool loopRange (int64_t& from, int64_t& to) const;   // the picked notes' span, else the clip
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
    Staff* staff = nullptr;
    QToolButton* staffButton = nullptr;   // the toggle: the staff notation or the piano roll
    QToolButton* notationButton = nullptr;
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
    // The transport (owner, 2026-10-30: "another transport control for the MIDI Editor and Staff
    // Notation Score"): over the roll and the staff alike, playing on the editor's own clock.
    TransportButton* playButton = nullptr;
    TransportButton* recordButton = nullptr;
    TransportButton* loopButton = nullptr;
    QLabel* timeLabel = nullptr;
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
    std::unique_ptr<AudioCapture> capture;   // the audio input, while humming
    QTimer* playTimer = nullptr;
    struct Timed { double seconds; uint8_t status, data1, data2; };
    std::vector<Timed> playing;
    size_t playAt = 0;
    double playFrom = 0.0;      // seconds: where the play started
    qint64 playStarted = 0;     // ms, the clock when it started
    QTimer* followTimer = nullptr;   // watching the tied transport
    double lastClock = 0.0;     // the tied transport's time at the last tick, to see it jump
    bool playingOriginal = false;
    int64_t playheadTick = -1;
    int64_t pressedAt = 0;      // the tick Play was pressed at, where Stop goes back to
    int64_t loopFrom = 0, loopTo = 0;   // while it loops
    midifile::SongPtr playSong;
    // Recording: the keys held, with where and how hard they went down.
    bool recording = false;
    struct Held { int64_t start; int velocity; double pitch; };
    std::map<int, Held> held;
};
