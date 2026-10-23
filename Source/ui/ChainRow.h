// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QFrame>
#include <QTimer>
#include <QPointer>

#include <functional>
#include <map>
#include <vector>

class QDial;
class QLineEdit;
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

// A Prism card's strip of rays: one bar each, lit by its level.
class PrismStrip final : public QWidget
{
public:
    struct Ray
    {
        QString name;
        bool heard = true, muted = false, soloed = false;
        float peak = 0.0f;
        int devices = 0;
    };

    explicit PrismStrip (QWidget* parent);
    void set (const std::vector<Ray>&);

protected:
    void paintEvent (QPaintEvent*) override;

private:
    std::vector<Ray> rays;
    std::vector<float> shownDb;
};

// One card in the device chain row (or in a ray of a Prism): a plugin, the "add" card, or the master card.
// A Prism's card shows its eight macros.
class DeviceCard final : public QFrame
{
    Q_OBJECT

public:
    enum class Kind { plugin, add, master };

    DeviceCard (AudioEngine&, Kind, ChainPath, QWidget* parent);

    Kind kind() const   { return cardKind; }
    int slot() const    { return place.slot; }
    const ChainPath& path() const { return place; }
    bool prism() const  { return isPrism; }
    int numKnobs() const { return (int) knobs.size(); }
    int parameterOf (int knob) const;       // the plugin parameter a knob turns, or -1

    static QString prismDeviceMime() { return QStringLiteral ("application/x-phi-prism-device"); }
    static QString prismSummary (const PrismInfo&);   // "Prism  Bands  150 / 1.5k"

    void setSelected (bool selected, bool rowFocused, int knob);
    void setInRange (bool);                 // part of a range picked with Shift, to group
    void setEditing (bool);                 // a Prism popped out for editing: lit where it docks
    void sync();                            // follows the engine: values, bypass, solo, level
    void nudge (int knob, double steps);    // one step is 1% (or one notch of a stepped parameter)
    void editValue (int knob, const QString& startText = {});  // type an exact value; Enter sets it, Esc cancels

Q_SIGNALS:
    void activated (int knob);              // clicked; knob is -1 for the card itself
    void changed();                         // this card changed the chain (bypass, solo...)
    void editRequested();
    void windowRequested();
    void addRequested();
    void valueEditFinished();
    void message (const QString&);

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    struct Knob
    {
        int parameter = -1;   // plugin parameter index; or masterGain, ditherDepth, ditherNoise
        int steps = 0;        // 0 = continuous
        QDial* dial = nullptr;
        QLabel* name = nullptr;
        QLabel* value = nullptr;
    };

    static constexpr int masterGain = -1, ditherDepth = -2, ditherNoise = -3;   // the master card's own knobs
    void addKnob (int parameter, const QString& name, int steps);
    void setKnobFromEngine (Knob&);
    void knobMoved (const Knob&, int position);
    void watch (QWidget*);
    void applyTypedValue (int knob, const QString& text);

    AudioEngine& engine;
    Kind cardKind;
    ChainPath place;
    bool isPrism = false;
    PrismStrip* rays = nullptr;
    bool inRange = false, editing = false;
    QLabel* title = nullptr;
    QLabel* detail = nullptr;
    QToolButton* power = nullptr;
    QToolButton* solo = nullptr;
    LevelBar* level = nullptr;
    QToolButton* windowButton = nullptr;
    std::vector<Knob> knobs;
    bool selected = false, focused = false, syncing = false;
    int selectedKnob = -1;
    QPoint pressPos;
    QLineEdit* valueEditor = nullptr;
};

// Ableton/Bitwig-style device chain along the bottom of the window: the Master
// Section's plugins as cards, signal flowing left to right into the master gain.
// Keys: Left/Right pick a card, Up/Down pick a knob, +/- (Shift for bigger steps,
// or Page Up/Down) turn it, F2 or a digit types an exact value (so does a double
// click), Enter edits all parameters, Insert adds a plugin after
// the card, Delete removes it, B bypasses, S solos, E opens the plugin's window,
// Ctrl+Left/Right moves the plugin, Esc returns to the waveform.
// Shift+Left/Right picks several plugins; Ctrl+G groups them into a Prism and
// Ctrl+Shift+G ungroups one. Enter on a Prism pops it out to edit (PrismEditor);
// it docks back into its card when done.
class ChainRow final : public QWidget
{
    Q_OBJECT

public:
    ChainRow (AudioEngine&, QWidget* home, QWidget* parent);

    // Shows the plugin browser for a slot and loads the choice; returns true when a plugin was loaded.
    std::function<bool (int slot)> choosePlugin;
    // Shows the plugin browser and returns the chosen plugin's id, or empty (for a Prism's rays).
    std::function<std::string()> pickPlugin;

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
    void groupSelection();
    void ungroupCurrent();
    void openPrism (int card);
    void showRange();
    std::pair<int, int> range() const;   // the cards picked with Shift, or just the current one
    int cardAt (const QPoint&) const;
    static bool handles (const QKeyEvent*);

    AudioEngine& engine;
    QWidget* home;
    QScrollArea* scroll;
    QWidget* strip;
    QHBoxLayout* stripLayout;
    std::vector<DeviceCard*> cards;
    int current = 0, knob = -1;
    int anchor = -1;   // where a Shift range starts; -1 for none
    std::map<std::uint64_t, QPointer<class PrismEditor>> editors;   // by Prism, popped out
    QString lastSignature;
    int failedSlots = 0;   // slots whose plugin stopped working, as bits, so each failure is announced once
    QTimer timer;
};
