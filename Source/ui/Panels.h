// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"
#include "../engine/AudioEngine.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTreeWidget>
#include <QWidget>
#include <vector>

class QDockWidget;
class QMainWindow;

// Window shortcuts such as Space, arrows and Delete would otherwise steal keys
// from panels. A panel keeps its navigation keys (plus any extra keys it
// handles itself), and Escape returns focus to the home widget.
// Focus follows the mouse inside PHINYX, like Hyprland does between windows:
// pointing at the waveform, the device chain or a panel gives it the keyboard.
// Only moves focus between these areas, never out of a text field or dialog.
class FocusFollowsMouse final : public QObject
{
public:
    explicit FocusFollowsMouse (QObject* parent) : QObject (parent) {}

    void setAreas (QList<QWidget*> a) { areas = std::move (a); }
    void setEnabled (bool on)         { enabled = on; }
    bool isEnabled() const            { return enabled; }

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    QWidget* areaOf (QWidget*) const;

    QList<QWidget*> areas;
    bool enabled = true;
};

class NavigationKeyGuard final : public QObject
{
public:
    NavigationKeyGuard (QWidget* home, QList<int> extraKeys, QObject* parent);

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    QWidget* homeWidget;
    QList<int> extra;
};

// A panel that lives in a dock of the main window or "pops out" into its own
// top-level window. On Hyprland a popped-out panel is an ordinary window, so it
// tiles next to the editor and can be moved to another monitor or workspace.
class DockablePanel final : public QObject
{
    Q_OBJECT

public:
    DockablePanel (QMainWindow& main, const QString& id, const QString& title, QWidget* content, Qt::DockWidgetArea);
    ~DockablePanel() override;

    QString id() const        { return panelId; }
    QWidget* content() const  { return contentWidget; }
    QDockWidget* dock() const { return dockWidget; }

    bool isShown() const;
    bool isPoppedOut() const  { return window != nullptr; }
    bool containsFocus() const;

    void show (bool giveFocus);
    void hide();
    void toggle();               // show and focus it, or hide it when it already has focus
    void setPoppedOut (bool);

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    void dockBack (bool keepVisible);

    QMainWindow& mainWindow;
    QString panelId, title;
    QWidget* contentWidget;
    QDockWidget* dockWidget;
    QWidget* window = nullptr;
};

// Marker and region list. Enter jumps to the marker, F2 renames it, Delete removes it.
class MarkerList final : public QTreeWidget
{
    Q_OBJECT

public:
    explicit MarkerList (QWidget* parent);
    void setMarkers (const std::vector<Marker>&, double sampleRate);
    int currentMarkerId() const;

Q_SIGNALS:
    void jumpRequested (int id);
    void renameRequested (int id);
    void deleteRequested (int id);

protected:
    void keyPressEvent (QKeyEvent*) override;
};

// Level and loudness meters fed by the engine: per-channel sample and true peak
// bars with peak hold, and momentary, short-term and integrated loudness (BS.1770).
class MeterPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit MeterPanel (QWidget* parent);

    void addBlocks (const std::vector<MeterBlock>&);  // call regularly, also with no blocks, so bars fall
    void reset();

    QSize sizeHint() const override { return { 260, 320 }; }

protected:
    void paintEvent (QPaintEvent*) override;

private:
    static constexpr int maxChannels = MeterBlock::maxChannels;

    int numChannels = 2;
    float level[maxChannels] {};         // displayed dB, falls over time
    float hold[maxChannels] {};          // held peak dB
    double holdAge[maxChannels] {};      // seconds since the hold was set
    float maxTruePeak = -200.0f, maxSamplePeak = -200.0f;
    std::vector<double> powers;          // every 100 ms block since reset, for integrated loudness
    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0;
    QElapsedTimer clock;
};
