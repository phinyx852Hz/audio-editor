// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QFrame>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <vector>

class DeviceCard;
class LevelBar;
class Potentiometer;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

// A Prism popped out of its card in the Device Chain to be edited: it rises out of
// the card, floating over the window, and sinks back into it when done (Esc, the
// Dock button, or Open on the card again). It is part of the window, not a window
// of its own, so it opens in the same place on every desktop, Wayland too.
//
// Along the top: the way in (a Prism inside a Prism opens in place, and the path
// back is shown), the split and its crossovers. Then the eight macros: Map arms a
// macro, and the next knob clicked in the rays is mapped to it; right-click a macro
// for MIDI learn, its mappings and their ranges. Then a row per ray: its gain, mute
// and solo, and its devices as cards. Plugins can be dragged between rays.
//
// Keys: Left/Right pick a device, Up/Down a ray, Shift+Left/Right several devices,
// Ctrl+G groups them into a Prism (Ctrl+Shift+G ungroups one), Insert adds a plugin,
// Delete removes it, B bypasses it, Enter or E opens it (its window, or the Prism),
// Ctrl+Left/Right moves it along its ray and Ctrl+Up/Down to the next ray, M mutes
// and S solos the ray, Ctrl+T adds a ray, F2 renames, Esc goes back out (and docks).
class PrismEditor final : public QFrame
{
    Q_OBJECT

public:
    PrismEditor (AudioEngine&, int slot, std::uint64_t uid, QWidget* window);

    std::function<std::string()> pickPlugin;                 // the plugin browser: an id, or empty
    std::function<QWidget* (std::uint64_t)> findCard;        // the card holding a Prism now

    void popOut (QWidget* card);
    void dock();      // sinks back into its card, then closes
    void dockNow();   // at once

Q_SIGNALS:
    void docked (bool returnFocus);
    void message (const QString&);
    void chainChanged();

protected:
    bool event (QEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    struct MacroView
    {
        QWidget* column = nullptr;
        Potentiometer* pot = nullptr;
        QLabel* name = nullptr;
        QLabel* value = nullptr;
        QLabel* midi = nullptr;
    };

    struct RayView
    {
        QFrame* frame = nullptr;
        QLabel* name = nullptr;
        Potentiometer* gain = nullptr;
        QLabel* gainText = nullptr;
        QToolButton* mute = nullptr;
        QToolButton* solo = nullptr;
        LevelBar* level = nullptr;
        QHBoxLayout* cardsLayout = nullptr;
        std::vector<DeviceCard*> cards;   // devices, then the add card
    };

    ChainPath prismPath() const { return ChainPath { slot, steps }; }
    bool follow();   // finds its Prism again (it may have moved); false when it is gone
    QString signature (const PrismInfo&) const;
    void rebuild();
    void sync();
    void buildMacros (QHBoxLayout*);
    void buildRay (int ray, const PrismRayInfo&);
    void select (int ray, int device);
    void showSelection();
    DeviceCard* currentCard() const;
    std::pair<int, int> range() const;

    void enter (int ray, int device);   // a Prism inside: opens in place
    void goOut (int levels);
    void addPlugin (int ray, int index);
    void openDevice (int ray, int device);
    void removeCurrent();
    void moveCurrent (int rayDelta, int deviceDelta);
    void groupSelection();
    void ungroupCurrent();
    void addRay();
    void macroMenu (int macro, const QPoint& globalPos);
    void editRange (int macro, int mapping);
    void renameMacro (int macro);
    void renameRay (int ray);
    void deviceKnobClicked (int ray, int device, int knob);
    QRect cardRect() const;
    int wantedHeight() const;   // the docked card, in the window's coordinates
    void animateTo (const QRect&, std::function<void()> then);
    static bool handles (const QKeyEvent*);

    AudioEngine& engine;
    int slot;
    const std::uint64_t uid;
    std::vector<std::pair<int, int>> steps;   // into Prisms inside this one
    std::vector<std::uint64_t> stepUids;      // which Prism each step led to, to notice when it is gone

    QWidget* header = nullptr;
    QHBoxLayout* crumbs = nullptr;
    QLineEdit* nameEdit = nullptr;
    QComboBox* splitBox = nullptr;
    std::vector<Potentiometer*> crossoverPots;
    std::vector<QLabel*> crossoverLabels;
    QToolButton* mapButton = nullptr;
    QWidget* body = nullptr;
    QScrollArea* raysScroll = nullptr;
    QWidget* raysHost = nullptr;
    QVBoxLayout* raysLayout = nullptr;
    std::vector<MacroView> macroViews;
    std::vector<RayView> rayViews;

    int ray = 0, device = 0, anchor = -1;
    int armed = -1;   // the macro Map mode maps the next knob to
    QString lastSignature;
    QPoint dragFrom;
    bool dragging = false, closing = false;
    QPointer<QWidget> dockCard;
    QTimer timer;
};
