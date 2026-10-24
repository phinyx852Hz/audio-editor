// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/MasterAssist.h"
#include "../engine/AudioEngine.h"

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTreeWidget;
class QTreeWidgetItem;

// The Master Assistant (Master menu): tunes the chain you built in the Master
// Section towards a reference track or a loudness preset. It only turns knobs,
// the ones ticked in its list, and never adds, removes or moves a plugin. Its
// suggestion is heard at once; Amount blends between your settings and its
// settings, A switches between them, Keep keeps what you hear and Revert (or
// Esc) puts every knob back. The core is assist:: (MasterAssist.h).
class MasterAssistant final : public QDialog
{
    Q_OBJECT

public:
    struct References
    {
        QStringList names;
        std::function<AudioDataPtr (int)> load;   // at your audio's sample rate; null when it could not be loaded
        int current = -1;
    };

    MasterAssistant (AudioEngine&, AudioDataPtr source, Frame start, Frame end, References,
                     std::function<void()> playStop, QWidget* parent);
    ~MasterAssistant() override;

    void reject() override;
    void accept() override;

private:
    struct Knob
    {
        ChainPath path;
        int index = 0;
        QString device, name, originalText, suggestedText;
        double original = 0.0, suggested = 0.0;
        QTreeWidgetItem* item = nullptr;
    };

    struct Outcome
    {
        assist::Result result;
        assist::Profile before, after, wholeSource, excerptSource;
        assist::Target target;            // for the excerpt
        std::string error;
    };

    void collectKnobs();
    void addKnobs (const ChainPath&, const MasterSlotInfo&, QTreeWidgetItem* parent);
    void targetChanged();
    void updateEnabled();
    bool buildTarget (assist::Target&);
    void suggest();
    void finished();
    void apply (double amount);           // 0 = your settings, 1 = the suggestion
    void setHearingYours (bool);
    void showMeasures (const Outcome&);
    void setRunning (bool);

    AudioEngine& engine;
    AudioDataPtr source;
    Frame rangeStart, rangeEnd;
    References references;
    std::function<void()> playStop;

    std::vector<Knob> knobs;
    std::vector<int> moving;              // indices into knobs of the ticked ones, for the run in progress
    int leftAlone = 0;                    // ticked, but not for this target
    bool suggested = false, hearingYours = false, running = false, closeWhenDone = false, closeAccepting = false;
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>> (false);
    std::shared_ptr<std::atomic<float>> progress = std::make_shared<std::atomic<float>> (0.0f);
    QFutureWatcher<Outcome> watcher;

    QComboBox* aim;
    QDoubleSpinBox* loudness;
    QDoubleSpinBox* ceiling;
    QComboBox* tone;
    QCheckBox *matchLoudness, *matchTone, *matchDynamics, *matchWidth;
    QComboBox* freedom;
    QTreeWidget* knobList;
    QTreeWidget* measures;
    QProgressBar* bar;
    QLabel* status;
    QPushButton *suggestButton, *abButton, *keepButton, *revertButton;
    QSlider* amount;
    QLabel* amountLabel;
};
