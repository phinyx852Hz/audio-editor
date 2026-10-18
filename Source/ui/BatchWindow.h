// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/Batch.h"

#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QTreeWidget;

// The Batch Processor window (Tools > Batch Processor): files, processing
// steps and outputs, run on every core in the background while you keep
// editing. Batches are saved as .phbatch files and the last one comes back.
class BatchWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit BatchWindow (QWidget* parent);
    ~BatchWindow() override;

    // Supplied by the main window: renders through its Master Section, and
    // the paths of the files open in its tabs.
    batch::MasterRender masterRender;
    std::function<QStringList()> openFiles;

    bool isRunning() const { return running; }
    void addFiles (const QStringList&);

Q_SIGNALS:
    void openRequested (const QString& path);   // double-click on a result

protected:
    void closeEvent (QCloseEvent*) override;
    void dragEnterEvent (class QDragEnterEvent*) override;
    void dropEvent (class QDropEvent*) override;

private:
    void refreshFiles();
    void refreshSteps();
    void refreshOutputs();
    void refreshMode();
    void showStep (int row);
    void addStep (const std::string& type);
    void editOutputs();
    void addFolder();
    void openBatch();
    void saveBatch();
    void start();
    void stop();
    void finish();
    void remember();
    QString reportPath() const;

    batch::Job job;
    bool running = false, filling = false;

    QComboBox* modeBox;
    QListWidget* fileList;
    QLabel* fileCount;
    QListWidget* stepList;
    QWidget* stepEditor;
    QFormLayout* stepForm;
    QWidget* stepsArea;
    QLabel* outputSummary;
    QPushButton* outputsButton;
    QWidget* outputsArea;
    QWidget* tagsArea;
    QWidget* renameArea;
    class QPlainTextEdit* tagTemplate;
    QLineEdit* renamePattern;
    QCheckBox* reportBox;
    QLineEdit* reportEdit;
    QSpinBox* threadsBox;
    QPushButton* runButton;
    QPushButton* openReportButton;
    QProgressBar* progress;
    QLabel* status;
    QTreeWidget* results;

    // The run: workers take files in turn; results come back to the UI thread.
    struct Run
    {
        batch::Job job;
        std::atomic<bool> cancel { false };
        std::atomic<size_t> next { 0 };
        std::vector<std::atomic<float>> progress;
        std::vector<batch::FileResult> results;
        std::atomic<int> done { 0 }, workersLeft { 0 };
        explicit Run (size_t files) : progress (files), results (files) {}
    };
    std::shared_ptr<Run> run;
    std::vector<std::thread> workers;
    QTimer* poll;
};
