// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/PhiForensics.h"
#include "../core/PluginMeasure.h"
#include "../engine/AudioEngine.h"
#include "../mixer/MixerEngine.h"

#include <QFutureWatcher>
#include <QWidget>

#include <array>
#include <atomic>
#include <memory>

class AnalyzerPlot;
class PluginProbe;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTimer;

// Measures a probe (copies of plugins, see PluginProbe) in the background, again
// whenever what it copies changes. Shared by the Plugin Analyzer and the Aliasing scope.
class ProbeRunner final : public QObject
{
    Q_OBJECT

public:
    struct Outcome
    {
        measure::Results results;
        forensic::Report forensics;
        std::string title;
    };

    ProbeRunner (AudioEngine&, QObject* parent);
    ~ProbeRunner() override;

    // What to measure (nullptr: nothing). It is synced and measured from the next tick.
    void setProbe (std::shared_ptr<PluginProbe>);
    void setSettings (const measure::Settings& s) { settings = s; dirty = true; }
    const measure::Settings& currentSettings() const { return settings; }
    void setLive (bool on) { live = on; }
    void setForensics (bool on) { forensics = on; }
    void measureNow() { dirty = true; tick(); }
    void cancel() { cancelled = true; }
    bool busy() const { return watcher.isRunning(); }
    double progress() const { return fraction.load(); }
    std::string lastError() const { return error; }

Q_SIGNALS:
    void started();
    void finished (const ProbeRunner::Outcome&);
    void problem (const QString&);

private:
    void tick();

    AudioEngine& engine;
    std::shared_ptr<PluginProbe> probe;
    std::vector<std::shared_ptr<PluginProbe>> retired;   // let go on this thread once nothing measures them
    measure::Settings settings;
    QFutureWatcher<Outcome> watcher;
    QTimer* timer;
    std::atomic<double> fraction { 0.0 };
    std::atomic<bool> cancelled { false };
    bool live = true, dirty = true, forensics = true;
    std::string error;
};

// Plugin Analyzer (Analyze > Plugin Analyzer, Ctrl+Alt+U; A on a Master Section slot):
// what a plugin does to sound, measured on a copy of it with its settings, again each
// time a setting changes (Live). Plugin Doctor's measurements and more, built into PHI:
// - Frequency: magnitude, phase and group delay from an exponential sweep;
// - Impulse: the impulse response, with the latency it reports and the one it has;
// - Harmonics: the spectrum of a steady tone, THD, THD+N, and H2-H5 over frequency;
// - Aliasing: harmonics folded back from above Nyquist, for tones up to 20 kHz;
// - Dynamics: level in against level out, and the attack and release of a step;
// - Modulation: how a steady tone's level and pitch move (LFO rates, gates);
// - Forensic Φ: whether the measurements show the golden ratio, weighed against chance.
// It measures a slot of the Master Section, the whole Master Section, or a mixer
// track's inserts. "Load a plugin..." puts a plugin into a free slot, bypassed, to try it.
class PluginAnalyzer final : public QWidget
{
    Q_OBJECT

public:
    PluginAnalyzer (AudioEngine&, QWidget* parent);
    ~PluginAnalyzer() override;

    void analyseSlot (int slot);
    void analyseMasterSection();
    void analyseInserts (const mixer::Inserts&, const QString& name);

Q_SIGNALS:
    void closed();
    void chainChanged();   // it loaded a plugin into the Master Section

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void closeEvent (class QCloseEvent*) override;
    void showEvent (class QShowEvent*) override;

private:
    void refreshSources();
    void sourceChosen();
    void loadPlugin();
    void applySettings();
    void display (const ProbeRunner::Outcome&);
    void showView();
    QString reportText() const;

    AudioEngine& engine;
    ProbeRunner* runner;
    QComboBox* source;
    QDoubleSpinBox* level;
    QComboBox* input;
    QCheckBox* liveBox;
    std::array<QCheckBox*, 5> tests {};
    QPushButton* measureButton;
    QPushButton* windowButton;
    QProgressBar* progressBar;
    QLabel* status;
    QTabWidget* tabs;
    QComboBox* phaseView;
    QComboBox* impulseView;
    AnalyzerPlot *magnitude, *phase, *impulse, *harmonicSpectrum, *harmonicSweep, *aliasSpectrum, *aliasTones, *transfer, *step, *envelope, *scan;
    QLabel *harmonicText, *aliasText, *dynamicsText, *modulationText, *verdict;
    QPlainTextEdit* forensicText;
    QTimer* poll;
    std::unique_ptr<ProbeRunner::Outcome> last;
    int slot = -1;                 // the slot measured; -1 the whole Master Section, -2 mixer inserts
    mixer::Inserts inserts;
    QString insertsName;
};
