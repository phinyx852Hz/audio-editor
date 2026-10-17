// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"
#include "CommandRegistry.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>

class QLabel;
class WaveformView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow (AudioEngine&);
    ~MainWindow() override;

    void openFile (const QString& path);

protected:
    void closeEvent (QCloseEvent*) override;

private:
    void registerCommands();
    void buildMenus();
    void updateStatus();
    void updateCommandStates();

    void showOpenDialog();
    void togglePlayback();
    void moveCursorBy (double seconds);
    void setCursor (double seconds);
    void openMenuBar();

    AudioEngine& engine;
    CommandRegistry commands;
    WaveformView* waveform;
    QLabel* statusLabel;
    QString currentFile;

    QFutureWatcher<std::shared_ptr<WaveformPeaks>> peakWatcher;
    std::shared_ptr<std::atomic<bool>> cancelPeaks;
};
