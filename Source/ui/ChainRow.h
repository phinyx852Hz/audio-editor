// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QFrame>
#include <QTimer>
#include <functional>
#include <vector>

class QDial;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QToolButton;

// Small vertical level bar on a device card.
class LevelBar final : public QWidget
{
public:
    explicit LevelBar (QWidget* parent);
    void push (float linearPeak);  // call regularly; falls back smoothly

protected:
    void paintEvent (QPaintEvent*) override;

private:
    float shownDb = -100.0f;
};

// One card in the device chain row: a plugin, the "add" card, or the master card.
class DeviceCard final : public QFrame
{
    Q_OBJECT

public:
    enum class Kind { plugin, add, master };

    DeviceCard (AudioEngine&, Kind, int slot, QWidget* parent);

    Kind kind() const   { return cardKind; }
    int slot() const    { return slotIndex; }
    int numKnobs() const { return (int) knobs.size(); }

    void setSelected (bool selected, bool rowFocused, int knob);
    void sync();                            // follows the engine: values, bypass, solo, level
    void nudge (int knob, double steps);    // one step is 1% (or one notch of a stepped parameter)

Q_SIGNALS:
    void activated (int knob);              // clicked; knob is -1 for the card itself
    void changed();                         // this card changed the chain (bypass, solo...)
    void editRequested();
    void windowRequested();
    void addRequested();

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    struct Knob
    {
        int parameter = -1;   // plugin parameter index; -1 = master gain
        int steps = 0;        // 0 = continuous
        QDial* dial = nullptr;
        QLabel* name = nullptr;
        QLabel* value = nullptr;
    };

    void addKnob (int parameter, const QString& name, int steps);
    void setKnobFromEngine (Knob&);
    void knobMoved (const Knob&, int position);
    void watch (QWidget*);

    AudioEngine& engine;
    Kind cardKind;
    int slotIndex;
    QLabel* title = nullptr;
    QLabel* detail = nullptr;
    QToolButton* power = nullptr;
    QToolButton* solo = nullptr;
    LevelBar* level = nullptr;
    std::vector<Knob> knobs;
    bool selected = false, focused = false, syncing = false;
    int selectedKnob = -1;
    QPoint pressPos;
};

// Ableton/Bitwig-style device chain along the bottom of the window: the Master
// Section's plugins as cards, signal flowing left to right into the master gain.
// Keys: Left/Right pick a card, Up/Down pick a knob, +/- (Shift for bigger steps,
// or Page Up/Down) turn it, Enter edits all parameters, Insert adds a plugin after
// the card, Delete removes it, B bypasses, S solos, E opens the plugin's window,
// Ctrl+Left/Right moves the plugin, Esc returns to the waveform.
class ChainRow final : public QWidget
{
    Q_OBJECT

public:
    ChainRow (AudioEngine&, QWidget* home, QWidget* parent);

    // Shows the plugin browser for a slot and loads the choice; returns true when a plugin was loaded.
    std::function<bool (int slot)> choosePlugin;

    void rebuild();

Q_SIGNALS:
    void chainChanged();
    void message (const QString&);

protected:
    bool event (QEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    void focusInEvent (QFocusEvent*) override;
    void focusOutEvent (QFocusEvent*) override;
    void dragEnterEvent (QDragEnterEvent*) override;
    void dragMoveEvent (QDragMoveEvent*) override;
    void dropEvent (QDropEvent*) override;

private:
    QString signature() const;
    void select (int card, int knob);
    void showSelection();
    DeviceCard* currentCard() const;
    int lastPluginSlot() const;
    void insertAfter (int slot);            // -1 = at the start
    void movePlugin (int from, int to);
    void removeCurrent();
    void toggleBypass();
    void toggleSolo();
    void editParameters (int slot);
    void showWindow (int slot);
    int cardAt (const QPoint&) const;
    static bool handles (const QKeyEvent*);

    AudioEngine& engine;
    QWidget* home;
    QScrollArea* scroll;
    QWidget* strip;
    QHBoxLayout* stripLayout;
    std::vector<DeviceCard*> cards;
    int current = 0, knob = -1;
    QString lastSignature;
    QTimer timer;
};
