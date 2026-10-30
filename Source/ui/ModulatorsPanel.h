// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QWidget>

#include <deque>
#include <vector>

class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;

// The Modulators panel, in the bottom zone beside the Device Chain, after Cubase's: the
// rack of modulators on the left (Add Modulator offers Cubase's kinds and Φ Flow), the
// chosen one's controls and a trace of what it gives in the middle, and on the right
// the parameters it moves, each between a low and a high end as a Prism macro does.
// Destinations are any parameter of the plugins in the Device Chain's slots; to move
// something inside a Prism, move one of its macros.
class ModulatorsPanel final : public QWidget
{
public:
    ModulatorsPanel (AudioEngine&, QWidget* parent);

    void refresh();   // after the chain changed

private:
    void rebuildList();
    void buildControls();
    void rebuildRoutes();
    void addModulator (modrack::Kind);
    void removeModulator();
    void renameModulator();
    void chooseDestination();
    void removeRoute();
    void push();   // the rack as edited, to the engine
    void tick();
    int chosen() const;

    AudioEngine& engine;
    std::vector<modrack::Modulator> rack;
    std::vector<modrack::Route> routes;
    QListWidget* list = nullptr;
    QWidget* controlArea = nullptr;
    QLabel* heading = nullptr;
    class ModulatorTrace* trace = nullptr;
    QTreeWidget* destinations = nullptr;
    QPushButton *addButton = nullptr, *removeButton = nullptr, *renameButton = nullptr;
    QPushButton *addRouteButton = nullptr, *removeRouteButton = nullptr;
    std::vector<QLabel*> valueLabels;
};
