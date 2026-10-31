// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The main editor's MIDI editor (owner, 2026-10-30): under the waveform, for one sound at a
// time. "The MIDI editor in this instance is not associated with a specific track at all. Just
// the MIDI editor", with an instrument of its own, and a bounce to audio "to focus on sound
// design of a single sound". It is the MIDI editor (MidiEditor.h) as a panel, its notes heard
// through one of the patchbay's synths, so the MIDI keyboard plays it and steps notes in. New,
// Open and Save work on a .mid file; Bounce to Audio renders the notes through a copy of the
// instrument, its tail included, into a new tab of the editor. The audio editor above and this
// panel each close on their own (View > Audio Editor, View > MIDI Editor). An audio file dropped on
// it becomes notes right away (owner, 2026-10-30): its melody, or its drums.
#include "../core/AudioData.h"

#include <QCoreApplication>
#include <QFrame>
#include <QPointer>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class AudioEngine;
class MidiEditor;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
namespace midifile { struct Song; }
namespace mixer { class Instrument; }
namespace patchbay { class PatchBay; }

class MidiPanel final : public QFrame
{
    Q_DECLARE_TR_FUNCTIONS (MidiPanel)

public:
    struct Host
    {
        AudioEngine* engine = nullptr;
        patchbay::PatchBay* patchBay = nullptr;
        std::function<void()> heard;                  // the patchbay heard (MainWindow::playInstruments)
        std::function<void (int synth)> keyboard;     // the MIDI keyboard plays this patchbay synth
        std::function<void (AudioDataPtr, const QString& name)> bounced;   // into a new tab
        std::function<void()> closeRequested;
        std::function<void (const QString&)> say;     // the status bar
    };

    MidiPanel (Host, QWidget* parent);
    ~MidiPanel() override;

    // A message from the MIDI keyboard, on the UI thread: step input when it is on.
    void midiIn (uint8_t status, uint8_t data1, uint8_t data2);
    int synthSlot() const { return slot; }
    // False when the user would rather keep unsaved notes (asked here).
    bool mayDiscard();
    // The panel closing (Ctrl+W, its ✕, View > MIDI Editor): playback stops and unsaved notes
    // are offered for saving; discarded, it opens empty next time. False: Cancel, it stays open.
    bool closing();

protected:
    void showEvent (QShowEvent*) override;
    // An audio file dropped from Find or the file manager becomes notes at once: its melody or its
    // drums, as the bar's Drop box says.
    void dragEnterEvent (QDragEnterEvent*) override;
    void dropEvent (QDropEvent*) override;

private:
    void newSong();
    void openSong();
    void saveSong();
    void load (const midifile::Song&, const QString& name);   // a new MidiEditor on these notes
    void instrumentMenu();
    bool ensureInstrument();
    bool choose (const QString& pluginId);    // empty: the built-in Synth
    bool place (std::shared_ptr<mixer::Instrument>);
    void bounce();
    std::vector<uint8_t> bytesNow() const;
    void refreshBar();

    Host host;
    QVBoxLayout* layout = nullptr;
    QPointer<MidiEditor> editor;
    QPushButton* instrumentButton = nullptr;
    QPushButton* editButton = nullptr;
    QLabel* nameLabel = nullptr;
    QComboBox* dropBox = nullptr;   // what a dropped audio file becomes: 0 its melody, 1 its drums
    std::shared_ptr<mixer::Instrument> instrument;
    QString instrumentId;      // the plugin's id; empty for the built-in Synth
    int slot = -1;             // the patchbay synth it plays as
    QString name;              // the .mid file's name, or empty
    QString path;
    std::vector<uint8_t> saved;   // the notes as last loaded or saved, to tell when they changed
};
