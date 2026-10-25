// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "PluginAnalyzer.h"

#include <QWidget>

// Aliasing scope: how much each tone from 1 kHz to 20 kHz folds back from above
// Nyquist after a whole chain, measured on copies of its plugins with their settings
// and measured again when a setting changes. It follows the Master Section (the
// mastering engine) unless it is given a mixer track's inserts. A bar per tone, worst
// colour first; double-click (or Enter) opens the Plugin Analyzer on the same chain.
class AliasScope final : public QWidget
{
    Q_OBJECT

public:
    AliasScope (AudioEngine&, QWidget* parent);

    void setInserts (const mixer::Inserts&);   // a mixer track's chain
    void followMaster();                       // the Master Section (the default)
    bool followsMaster() const { return master; }

    QSize sizeHint() const override { return { 320, 120 }; }
    QSize minimumSizeHint() const override { return { 160, 72 }; }

Q_SIGNALS:
    void analyzeRequested();

protected:
    void paintEvent (class QPaintEvent*) override;
    void mouseDoubleClickEvent (class QMouseEvent*) override;
    void keyPressEvent (class QKeyEvent*) override;
    void showEvent (class QShowEvent*) override;
    void hideEvent (class QHideEvent*) override;

private:
    void restart();
    void display (const ProbeRunner::Outcome&);

    AudioEngine& engine;
    ProbeRunner* runner;
    std::vector<measure::AliasPoint> points;
    QString title, state;
    double rate = 48000.0;
    bool master = true;
    mixer::Inserts inserts;
};
