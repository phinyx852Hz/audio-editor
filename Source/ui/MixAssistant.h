// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../mixer/MixAssist.h"
#include "../mixer/MixerEngine.h"

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>
#include <functional>
#include <map>
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

// The Mix Assistant (Mixer menu): sets the faders and pans of the mix you made so
// each track sits where its role suggests, with headroom for mastering and, given
// a reference, its tonal balance. It bounces every track it hears, then fits the
// faders in memory (mixassist::, MixAssist.h). It never touches clips, instruments,
// trim or the patchbay. Overlaps where one track hides another are listed; Apply EQ
// Cuts puts them in a Φ EQ in the hiding track's inserts. As in the Master Assistant:
// Amount, A to compare, Keep or Revert.
class MixAssistant final : public QDialog
{
    Q_OBJECT

public:
    struct References
    {
        QStringList names;
        std::function<AudioDataPtr (int)> load;
        int current = -1;
    };

    MixAssistant (mixer::Engine&, QStringList trackNames, References, std::function<void()> playStop,
                  std::function<void()> stripsChanged, QWidget* parent);
    ~MixAssistant() override;

    // Puts cuts (frequency in Hz, dB) in a Φ EQ in the track's inserts; an error, or empty.
    std::function<QString (int track, const std::vector<std::pair<double, double>>& cuts)> applyEq;

    void reject() override;
    void accept() override;

private:
    struct Row
    {
        int track = 0;
        QString name;
        double faderDb = 0.0, pan = 0.0;               // yours
        double suggestedDb = 0.0, suggestedPan = 0.0;
        bool heard = true;                             // not muted or soloed out
        QComboBox* role = nullptr;
        QCheckBox *moveFader = nullptr, *movePan = nullptr;
    };

    struct Outcome
    {
        mixassist::Suggestion suggestion;
        std::vector<int> tracks;                       // rows the suggestion's entries belong to
        std::string error;
    };

    void suggest();
    void finished();
    void apply (double amount);
    void setHearingYours (bool);
    void setRunning (bool);
    void showResult (const Outcome&);

    mixer::Engine& engine;
    References references;
    std::function<void()> playStop, stripsChanged;
    std::vector<Row> rows;
    bool suggested = false, hearingYours = false, running = false, closeWhenDone = false, closeAccepting = false;
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>> (false);
    std::shared_ptr<std::atomic<float>> progress = std::make_shared<std::atomic<float>> (0.0f);
    QFutureWatcher<Outcome> watcher;

    QTreeWidget* table;
    QDoubleSpinBox* loudness;
    QDoubleSpinBox* ceiling;
    QComboBox* reference;
    QCheckBox *balanceBox, *panBox, *keepPansBox;
    QLabel* overlaps;
    QPushButton* eqButton;
    std::map<int, std::vector<std::pair<double, double>>> cuts;   // by row, from the last suggestion
    QProgressBar* bar;
    QLabel* status;
    QPushButton *suggestButton, *abButton, *keepButton;
    QSlider* amount;
    QLabel* amountLabel;
};
