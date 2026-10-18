// SPDX-License-Identifier: AGPL-3.0-only
#include "BatchWindow.h"
#include "ExportDialog.h"
#include "MetadataEditor.h"

#include "../engine/AudioEngine.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QPointer>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <fstream>

namespace
{
    QString qs (const std::string& s) { return QString::fromStdString (s); }

    QString db (double v) { return v <= -199.0 ? QStringLiteral ("-inf") : QString::number (v, 'f', 1); }

    const char* whenNames[] { QT_TRANSLATE_NOOP ("BatchWindow", "Always"),
                              QT_TRANSLATE_NOOP ("BatchWindow", "Only if the true peak is above (dBTP)"),
                              QT_TRANSLATE_NOOP ("BatchWindow", "Only if the peak is above (dBFS)"),
                              QT_TRANSLATE_NOOP ("BatchWindow", "Only if louder than (LUFS)"),
                              QT_TRANSLATE_NOOP ("BatchWindow", "Only if quieter than (LUFS)"),
                              QT_TRANSLATE_NOOP ("BatchWindow", "Only if the sample rate is not (Hz)") };

    // Ready-made batches for the Steps menu's presets.
    struct Preset { const char* name; std::vector<std::string> steps; };
    const Preset presets[] {
        { QT_TRANSLATE_NOOP ("BatchWindow", "Streaming master: -14 LUFS, -1 dBTP"), { "normalizeLoudness" } },
        { QT_TRANSLATE_NOOP ("BatchWindow", "Podcast: trim, -16 LUFS, fades"), { "trimSilence", "normalizeLoudness", "fadeIn", "fadeOut" } },
        { QT_TRANSLATE_NOOP ("BatchWindow", "Clean up recordings: DC, DeClick, DeHum"), { "dcOffset", "declick", "dehum" } },
        { QT_TRANSLATE_NOOP ("BatchWindow", "Through the Master Section"), { "master" } },
    };
}

BatchWindow::BatchWindow (QWidget* parent)
    : QWidget (parent, Qt::Window)
{
    setWindowTitle (tr ("Batch Processor"));
    setAcceptDrops (true);
    resize (1100, 760);

    // Top: what the batch does, open and save.
    modeBox = new QComboBox (this);
    modeBox->addItem (tr ("Process and export every file"), (int) batch::Mode::render);
    modeBox->addItem (tr ("Write tags only (files stay where they are)"), (int) batch::Mode::tagsOnly);
    modeBox->addItem (tr ("Rename files from their tags"), (int) batch::Mode::rename);
    modeBox->addItem (tr ("Measure only: loudness and peak report"), (int) batch::Mode::report);
    auto* modeLabel = new QLabel (tr ("&Job:"), this);
    modeLabel->setBuddy (modeBox);
    auto* openButton = new QPushButton (tr ("&Open Batch..."), this);
    auto* saveButton = new QPushButton (tr ("Sa&ve Batch..."), this);
    auto* top = new QHBoxLayout;
    top->addWidget (modeLabel);
    top->addWidget (modeBox, 1);
    top->addWidget (openButton);
    top->addWidget (saveButton);

    // Files
    fileList = new QListWidget (this);
    fileList->setSelectionMode (QAbstractItemView::ExtendedSelection);
    fileList->setAccessibleName (tr ("Files"));
    fileList->setToolTip (tr ("Drop files or folders here. Insert adds files, Delete removes the chosen ones."));
    fileCount = new QLabel (this);
    auto* addFilesButton = new QPushButton (tr ("Add &Files..."), this);
    auto* addFolderButton = new QPushButton (tr ("Add Fol&der..."), this);
    auto* addOpenButton = new QPushButton (tr ("Add Open &Tabs"), this);
    auto* removeFilesButton = new QPushButton (tr ("Remove"), this);
    auto* clearButton = new QPushButton (tr ("Clear"), this);
    auto* fileButtons = new QHBoxLayout;
    for (auto* b : { addFilesButton, addFolderButton, addOpenButton, removeFilesButton, clearButton })
        fileButtons->addWidget (b);
    auto* filesBox = new QGroupBox (tr ("Files"), this);
    auto* filesLayout = new QVBoxLayout (filesBox);
    filesLayout->addWidget (fileList, 1);
    filesLayout->addWidget (fileCount);
    filesLayout->addLayout (fileButtons);

    // Steps
    stepsArea = new QGroupBox (tr ("Processing, in order"), this);
    stepList = new QListWidget (stepsArea);
    stepList->setAccessibleName (tr ("Steps"));
    stepList->setToolTip (tr ("Insert adds a step, Delete removes it, Ctrl+Up and Ctrl+Down move it"));
    auto* addStepButton = new QToolButton (stepsArea);
    addStepButton->setText (tr ("&Add Step"));
    addStepButton->setPopupMode (QToolButton::InstantPopup);
    auto* stepMenu = new QMenu (addStepButton);
    for (const auto& t : batch::stepTypes())
    {
        auto* a = stepMenu->addAction (tr (t.name.c_str()));
        a->setToolTip (qs (t.description));
        connect (a, &QAction::triggered, this, [this, id = t.id] { addStep (id); });
    }
    stepMenu->addSeparator();
    auto* presetMenu = stepMenu->addMenu (tr ("Start from a &Preset"));
    for (const auto& p : presets)
        connect (presetMenu->addAction (tr (p.name)), &QAction::triggered, this, [this, &p]
        {
            job.steps.clear();
            for (const auto& id : p.steps)
                job.steps.push_back (batch::makeStep (id));
            // Podcasts aim lower than music.
            if (std::string (p.name).rfind ("Podcast", 0) == 0)
                job.steps[1].values["target"] = -16.0;
            refreshSteps();
            stepList->setCurrentRow (0);
        });
    addStepButton->setMenu (stepMenu);
    auto* removeStepButton = new QPushButton (tr ("Remove"), stepsArea);
    auto* upButton = new QPushButton (tr ("Up"), stepsArea);
    auto* downButton = new QPushButton (tr ("Down"), stepsArea);
    auto* stepButtons = new QHBoxLayout;
    stepButtons->addWidget (addStepButton);
    stepButtons->addWidget (removeStepButton);
    stepButtons->addWidget (upButton);
    stepButtons->addWidget (downButton);
    stepButtons->addStretch (1);
    stepEditor = new QWidget (stepsArea);
    stepForm = new QFormLayout (stepEditor);
    stepForm->setContentsMargins (0, 0, 0, 0);
    auto* stepsLayout = new QVBoxLayout (stepsArea);
    stepsLayout->addWidget (stepList, 1);
    stepsLayout->addLayout (stepButtons);
    stepsLayout->addWidget (stepEditor);

    // Tags-only and rename settings take the steps' place in those modes.
    tagsArea = new QGroupBox (tr ("Tag template"), this);
    tagTemplate = new QPlainTextEdit (tagsArea);
    tagTemplate->setPlaceholderText (tr ("One per line, for example:\nartist=PHINYX Band\nalbum=Live 2026\ntitle={filename}\ncomment=Mastered {today}"));
    tagTemplate->setToolTip (MetadataEditor::variablesHelp() + tr ("\nAlso {lufs}, {truepeak} and {lra}, measured from each file."));
    auto* templatePick = new QComboBox (tagsArea);
    templatePick->addItem (tr ("Fill from a saved template..."));
    for (const auto& name : MetadataEditor::templateNames())
        templatePick->addItem (name, MetadataEditor::templateText (name));
    connect (templatePick, &QComboBox::activated, this, [this, templatePick] (int i)
    {
        if (i > 0)
            tagTemplate->setPlainText (templatePick->itemData (i).toString());
        templatePick->setCurrentIndex (0);
    });
    auto* tagsLayout = new QVBoxLayout (tagsArea);
    tagsLayout->addWidget (tagTemplate, 1);
    tagsLayout->addWidget (templatePick);
    tagsLayout->addWidget (new QLabel (tr ("Lossy files get only their tags rewritten; their audio is not encoded again."), tagsArea));

    renameArea = new QGroupBox (tr ("New names"), this);
    renamePattern = new QLineEdit (renameArea);
    renamePattern->setToolTip (MetadataEditor::variablesHelp());
    auto* renameLayout = new QVBoxLayout (renameArea);
    renameLayout->addWidget (new QLabel (tr ("Name pattern (the extension stays):"), renameArea));
    renameLayout->addWidget (renamePattern);
    renameLayout->addWidget (new QLabel (tr ("For example {track} {title}, or {artist} - {title}. A file is left alone when a tag it needs is empty or the name is taken."), renameArea));
    static_cast<QLabel*> (renameLayout->itemAt (2)->widget())->setWordWrap (true);
    renameLayout->addStretch (1);

    auto* splitter = new QSplitter (this);
    splitter->addWidget (filesBox);
    auto* right = new QWidget (this);
    auto* rightLayout = new QVBoxLayout (right);
    rightLayout->setContentsMargins (0, 0, 0, 0);
    rightLayout->addWidget (stepsArea);
    rightLayout->addWidget (tagsArea);
    rightLayout->addWidget (renameArea);
    splitter->addWidget (right);
    splitter->setStretchFactor (0, 1);
    splitter->setStretchFactor (1, 1);

    // Outputs, report and threads.
    outputsArea = new QWidget (this);
    outputSummary = new QLabel (outputsArea);
    outputSummary->setWordWrap (true);
    outputsButton = new QPushButton (tr ("&Outputs..."), outputsArea);
    outputsButton->setToolTip (tr ("The files written for each input: formats, loudness, names and folders, as in Export"));
    auto* outputsLayout = new QHBoxLayout (outputsArea);
    outputsLayout->setContentsMargins (0, 0, 0, 0);
    outputsLayout->addWidget (new QLabel (tr ("Writes:"), outputsArea));
    outputsLayout->addWidget (outputSummary, 1);
    outputsLayout->addWidget (outputsButton);

    reportBox = new QCheckBox (tr ("Write a &report (CSV):"), this);
    reportEdit = new QLineEdit (this);
    reportEdit->setPlaceholderText (tr ("Beside the first file"));
    auto* reportBrowse = new QPushButton (tr ("Browse..."), this);
    threadsBox = new QSpinBox (this);
    threadsBox->setRange (0, 64);
    threadsBox->setSpecialValueText (tr ("All cores (%1)").arg (std::max (1u, std::thread::hardware_concurrency())));
    auto* threadsLabel = new QLabel (tr ("At &once:"), this);
    threadsLabel->setBuddy (threadsBox);
    auto* reportRow = new QHBoxLayout;
    reportRow->addWidget (reportBox);
    reportRow->addWidget (reportEdit, 1);
    reportRow->addWidget (reportBrowse);
    reportRow->addSpacing (20);
    reportRow->addWidget (threadsLabel);
    reportRow->addWidget (threadsBox);

    // Run and results.
    runButton = new QPushButton (tr ("&Run"), this);
    runButton->setToolTip (tr ("Ctrl+Enter"));
    openReportButton = new QPushButton (tr ("Open Re&port"), this);
    openReportButton->setEnabled (false);
    progress = new QProgressBar (this);
    progress->setRange (0, 1000);
    status = new QLabel (this);
    auto* runRow = new QHBoxLayout;
    runRow->addWidget (runButton);
    runRow->addWidget (progress, 1);
    runRow->addWidget (status);
    runRow->addWidget (openReportButton);
    results = new QTreeWidget (this);
    results->setRootIsDecorated (false);
    results->setHeaderLabels ({ tr ("File"), tr ("Result"), tr ("Before"), tr ("After"), tr ("Written"), tr ("Notes") });
    results->setToolTip (tr ("Double-click a file to open it"));

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (top);
    layout->addWidget (splitter, 3);
    layout->addWidget (outputsArea);
    layout->addLayout (reportRow);
    layout->addLayout (runRow);
    layout->addWidget (results, 2);

    poll = new QTimer (this);
    poll->setInterval (100);

    // Wiring
    connect (modeBox, &QComboBox::activated, this, [this] { job.mode = (batch::Mode) modeBox->currentData().toInt(); refreshMode(); });
    connect (openButton, &QPushButton::clicked, this, &BatchWindow::openBatch);
    connect (saveButton, &QPushButton::clicked, this, &BatchWindow::saveBatch);
    connect (addFilesButton, &QPushButton::clicked, this, [this]
    {
        QStringList patterns;
        for (const auto& e : AudioEngine::readableExtensions())
            patterns << QStringLiteral ("*.") + qs (e);
        const auto chosen = QFileDialog::getOpenFileNames (this, tr ("Add Files"), QSettings().value (QStringLiteral ("batch/folder")).toString(),
                                                           tr ("Audio files (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
        if (! chosen.isEmpty())
        {
            QSettings().setValue (QStringLiteral ("batch/folder"), QFileInfo (chosen.first()).absolutePath());
            addFiles (chosen);
        }
    });
    connect (addFolderButton, &QPushButton::clicked, this, &BatchWindow::addFolder);
    connect (addOpenButton, &QPushButton::clicked, this, [this] { if (openFiles) addFiles (openFiles()); });
    auto removeFiles = [this]
    {
        if (running)
            return;
        auto rows = fileList->selectedItems();
        std::vector<int> indexes;
        for (auto* item : rows)
            indexes.push_back (fileList->row (item));
        std::sort (indexes.rbegin(), indexes.rend());
        for (const auto i : indexes)
            job.files.erase (job.files.begin() + i);
        refreshFiles();
    };
    connect (removeFilesButton, &QPushButton::clicked, this, removeFiles);
    connect (clearButton, &QPushButton::clicked, this, [this] { if (! running) { job.files.clear(); refreshFiles(); } });
    connect (stepList, &QListWidget::currentRowChanged, this, &BatchWindow::showStep);
    auto removeStep = [this]
    {
        const auto row = stepList->currentRow();
        if (running || row < 0 || row >= (int) job.steps.size())
            return;
        job.steps.erase (job.steps.begin() + row);
        refreshSteps();
        stepList->setCurrentRow (std::min (row, (int) job.steps.size() - 1));
    };
    auto moveStep = [this] (int by)
    {
        const auto row = stepList->currentRow();
        const auto to = row + by;
        if (running || row < 0 || row >= (int) job.steps.size() || to < 0 || to >= (int) job.steps.size())
            return;
        std::swap (job.steps[(size_t) row], job.steps[(size_t) to]);
        refreshSteps();
        stepList->setCurrentRow (to);
    };
    connect (removeStepButton, &QPushButton::clicked, this, removeStep);
    connect (upButton, &QPushButton::clicked, this, [moveStep] { moveStep (-1); });
    connect (downButton, &QPushButton::clicked, this, [moveStep] { moveStep (1); });
    connect (tagTemplate, &QPlainTextEdit::textChanged, this, [this] { job.tagTemplate = tagTemplate->toPlainText().toStdString(); });
    connect (renamePattern, &QLineEdit::textEdited, this, [this] (const QString& t) { job.renamePattern = t.toStdString(); });
    connect (outputsButton, &QPushButton::clicked, this, &BatchWindow::editOutputs);
    connect (reportBox, &QCheckBox::toggled, this, [this] (bool on) { reportEdit->setEnabled (on); });
    connect (reportBrowse, &QPushButton::clicked, this, [this]
    {
        const auto path = QFileDialog::getSaveFileName (this, tr ("Report"), reportPath(), tr ("CSV (*.csv)"));
        if (! path.isEmpty())
        {
            reportBox->setChecked (true);
            reportEdit->setText (path);
        }
    });
    connect (threadsBox, &QSpinBox::valueChanged, this, [this] (int n) { job.threads = n; });
    connect (runButton, &QPushButton::clicked, this, [this] { running ? stop() : start(); });
    connect (openReportButton, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl (QUrl::fromLocalFile (reportPath())); });
    connect (results, &QTreeWidget::itemDoubleClicked, this, [this] (QTreeWidgetItem* item)
    {
        const auto path = item->data (0, Qt::UserRole).toString();
        if (! path.isEmpty())
            Q_EMIT openRequested (path);
    });
    connect (poll, &QTimer::timeout, this, [this]
    {
        if (run == nullptr)
            return;
        float sum = (float) run->done.load();
        for (size_t i = 0; i < run->progress.size(); ++i)
        {
            const auto p = run->progress[i].load();
            if (p < 1.0f)
                sum += p;
        }
        progress->setValue ((int) (1000.0f * std::min (1.0f, sum / (float) std::max<size_t> (1, run->progress.size()))));
        if (run->workersLeft.load() == 0)
            finish();
    });

    // Keys
    auto key = [this] (QKeySequence k, QWidget* w, std::function<void()> fn)
    {
        auto* s = new QShortcut (k, w, nullptr, nullptr, Qt::WidgetShortcut);
        connect (s, &QShortcut::activated, this, std::move (fn));
    };
    key (QKeySequence (Qt::Key_Insert), fileList, [addFilesButton] { addFilesButton->click(); });
    key (QKeySequence (Qt::Key_Delete), fileList, removeFiles);
    key (QKeySequence (Qt::Key_Insert), stepList, [addStepButton] { addStepButton->showMenu(); });
    key (QKeySequence (Qt::Key_Delete), stepList, removeStep);
    key (QKeySequence (Qt::CTRL | Qt::Key_Up), stepList, [moveStep] { moveStep (-1); });
    key (QKeySequence (Qt::CTRL | Qt::Key_Down), stepList, [moveStep] { moveStep (1); });
    auto* runKey = new QShortcut (QKeySequence (Qt::CTRL | Qt::Key_Return), this);
    connect (runKey, &QShortcut::activated, runButton, &QPushButton::click);
    auto* closeKey = new QShortcut (QKeySequence (Qt::CTRL | Qt::Key_W), this);
    connect (closeKey, &QShortcut::activated, this, &QWidget::close);

    // The last batch comes back.
    std::string error;
    const auto last = QSettings().value (QStringLiteral ("batch/last")).toString().toStdString();
    if (! last.empty())
        job = batch::fromJson (last, error);
    if (job.outputs.empty())
        job.outputs = ExportDialog::remembered();
    filling = true;
    modeBox->setCurrentIndex (std::max (0, modeBox->findData ((int) job.mode)));
    tagTemplate->setPlainText (qs (job.tagTemplate));
    renamePattern->setText (qs (job.renamePattern));
    reportBox->setChecked (! job.reportPath.empty());
    reportEdit->setText (qs (job.reportPath));
    reportEdit->setEnabled (reportBox->isChecked());
    threadsBox->setValue (job.threads);
    filling = false;
    refreshFiles();
    refreshSteps();
    refreshOutputs();
    refreshMode();
    stepList->setCurrentRow (0);
}

BatchWindow::~BatchWindow()
{
    if (run != nullptr)
        run->cancel = true;
    for (auto& w : workers)
        if (w.joinable())
            w.join();
}

//==============================================================================
void BatchWindow::addFiles (const QStringList& paths)
{
    if (running)
        return;
    for (const auto& p : paths)
    {
        const QFileInfo info (p);
        if (info.isDir())
        {
            for (const auto& f : batch::filesIn (info.absoluteFilePath().toStdString(), true))
                if (std::find (job.files.begin(), job.files.end(), f) == job.files.end())
                    job.files.push_back (f);
            continue;
        }
        const auto path = info.absoluteFilePath().toStdString();
        if (info.isFile() && std::find (job.files.begin(), job.files.end(), path) == job.files.end())
            job.files.push_back (path);
    }
    refreshFiles();
}

void BatchWindow::addFolder()
{
    const auto folder = QFileDialog::getExistingDirectory (this, tr ("Add Folder"), QSettings().value (QStringLiteral ("batch/folder")).toString());
    if (folder.isEmpty())
        return;
    QSettings().setValue (QStringLiteral ("batch/folder"), folder);
    const auto subfolders = QMessageBox::question (this, tr ("Add Folder"), tr ("Include the folders inside it too?")) == QMessageBox::Yes;
    bool ok = false;
    const auto filter = QInputDialog::getText (this, tr ("Add Folder"), tr ("Only names containing (leave empty for every audio file):"),
                                               QLineEdit::Normal, {}, &ok);
    if (! ok)
        return;
    const auto found = batch::filesIn (folder.toStdString(), subfolders, filter.toStdString());
    for (const auto& f : found)
        if (std::find (job.files.begin(), job.files.end(), f) == job.files.end())
            job.files.push_back (f);
    refreshFiles();
    status->setText (tr ("Added %n file(s)", nullptr, (int) found.size()));
}

void BatchWindow::dragEnterEvent (QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() && ! running)
        e->acceptProposedAction();
}

void BatchWindow::dropEvent (QDropEvent* e)
{
    QStringList paths;
    for (const auto& url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    addFiles (paths);
}

void BatchWindow::refreshFiles()
{
    fileList->clear();
    for (const auto& f : job.files)
    {
        const QFileInfo info (qs (f));
        auto* item = new QListWidgetItem (info.fileName(), fileList);
        item->setToolTip (info.absoluteFilePath());
    }
    fileCount->setText (job.files.empty() ? tr ("No files yet: add some, or drop files and folders here")
                                          : tr ("%n file(s)", nullptr, (int) job.files.size()));
    remember();
}

void BatchWindow::refreshSteps()
{
    const QSignalBlocker block (stepList);
    const auto row = stepList->currentRow();
    stepList->clear();
    for (const auto& s : job.steps)
    {
        auto* item = new QListWidgetItem (qs (batch::describe (s)), stepList);
        if (! s.enabled)
            item->setForeground (palette().color (QPalette::Disabled, QPalette::Text));
    }
    if (job.steps.empty())
    {
        auto* item = new QListWidgetItem (tr ("No steps: files are only converted. Insert adds one."), stepList);
        item->setFlags (Qt::NoItemFlags);
    }
    stepList->setCurrentRow (std::clamp (row, -1, (int) job.steps.size() - 1));
    showStep (stepList->currentRow());
    remember();
}

void BatchWindow::addStep (const std::string& type)
{
    const auto row = stepList->currentRow();
    const auto at = row < 0 || row >= (int) job.steps.size() ? (int) job.steps.size() : row + 1;
    job.steps.insert (job.steps.begin() + at, batch::makeStep (type));
    refreshSteps();
    stepList->setCurrentRow (at);
    stepList->setFocus();
}

void BatchWindow::showStep (int row)
{
    while (stepForm->rowCount() > 0)
        stepForm->removeRow (0);
    if (row < 0 || row >= (int) job.steps.size())
        return;
    const auto* type = batch::findStep (job.steps[(size_t) row].type);
    if (type == nullptr)
        return;

    auto changed = [this, row]
    {
        if (row < stepList->count())
            stepList->item (row)->setText (qs (batch::describe (job.steps[(size_t) row])));
        remember();
    };

    auto* on = new QCheckBox (tr ("On"));
    on->setChecked (job.steps[(size_t) row].enabled);
    on->setToolTip (qs (type->description));
    connect (on, &QCheckBox::toggled, this, [this, row, changed] (bool v) { job.steps[(size_t) row].enabled = v; changed(); });
    stepForm->addRow (tr ("<b>%1</b>").arg (tr (type->name.c_str())), on);

    for (const auto& p : type->parameters)
    {
        const auto value = job.steps[(size_t) row].value (p.id);
        QWidget* editor;
        if (! p.choices.empty())
        {
            auto* box = new QComboBox;
            for (const auto& c : p.choices)
                box->addItem (tr (c.c_str()));
            box->setCurrentIndex ((int) value);
            connect (box, &QComboBox::currentIndexChanged, this, [this, row, id = p.id, changed] (int i)
            {
                job.steps[(size_t) row].values[id] = i;
                changed();
            });
            editor = box;
        }
        else
        {
            auto* spin = new QDoubleSpinBox;
            spin->setRange (p.minimum, p.maximum);
            spin->setSingleStep (p.step);
            spin->setDecimals (p.step < 0.1 ? 2 : p.step < 1.0 ? 1 : 0);
            spin->setSuffix (qs (p.suffix));
            spin->setValue (value);
            connect (spin, &QDoubleSpinBox::valueChanged, this, [this, row, id = p.id, changed] (double v)
            {
                job.steps[(size_t) row].values[id] = v;
                changed();
            });
            editor = spin;
        }
        stepForm->addRow (tr (p.label.c_str()) + QLatin1Char (':'), editor);
    }

    auto* when = new QComboBox;
    for (const auto* name : whenNames)
        when->addItem (tr (name));
    when->setCurrentIndex ((int) job.steps[(size_t) row].when);
    auto* whenValue = new QDoubleSpinBox;
    whenValue->setRange (-200.0, 384000.0);
    whenValue->setDecimals (1);
    whenValue->setValue (job.steps[(size_t) row].whenValue);
    whenValue->setEnabled (when->currentIndex() > 0);
    auto* whenRow = new QHBoxLayout;
    whenRow->addWidget (when, 1);
    whenRow->addWidget (whenValue);
    connect (when, &QComboBox::currentIndexChanged, this, [this, row, whenValue, changed] (int i)
    {
        auto& s = job.steps[(size_t) row];
        s.when = (batch::When) i;
        // A sensible starting value for each condition.
        const double defaults[] { 0.0, -1.0, -1.0, -14.0, -14.0, 44100.0 };
        if (i > 0)
            whenValue->setValue (defaults[i]);
        whenValue->setEnabled (i > 0);
        changed();
    });
    connect (whenValue, &QDoubleSpinBox::valueChanged, this, [this, row, changed] (double v) { job.steps[(size_t) row].whenValue = v; changed(); });
    stepForm->addRow (tr ("Run:"), whenRow);
}

void BatchWindow::refreshOutputs()
{
    QStringList names;
    for (const auto& o : job.outputs)
    {
        const auto* f = codec::find (o.formatId);
        auto text = qs (o.name.empty() && f != nullptr ? f->name : o.name);
        if (o.normalize)
            text += tr (" at %1 LUFS").arg (o.targetLufs, 0, 'f', 1);
        names << text;
    }
    outputSummary->setText (names.isEmpty() ? tr ("nothing yet: choose Outputs") : names.join (QStringLiteral (", ")));
    remember();
}

void BatchWindow::refreshMode()
{
    const auto mode = job.mode;
    stepsArea->setVisible (mode == batch::Mode::render);
    outputsArea->setVisible (mode == batch::Mode::render);
    tagsArea->setVisible (mode == batch::Mode::tagsOnly);
    renameArea->setVisible (mode == batch::Mode::rename);
    if (mode == batch::Mode::report && ! filling)
        reportBox->setChecked (true);
    remember();
}

void BatchWindow::editOutputs()
{
    ExportDialog::Context context;
    context.forBatch = true;
    context.outputs = job.outputs;
    context.sourcePath = job.files.empty() ? QString() : qs (job.files.front());
    ExportDialog dialog (context, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    job.outputs = dialog.outputs();
    refreshOutputs();
}

//==============================================================================
void BatchWindow::remember()
{
    if (filling)
        return;
    auto copy = job;
    copy.reportPath = reportBox->isChecked() ? reportEdit->text().toStdString() : std::string();
    QSettings().setValue (QStringLiteral ("batch/last"), qs (batch::toJson (copy)));
}

void BatchWindow::openBatch()
{
    const auto path = QFileDialog::getOpenFileName (this, tr ("Open Batch"), QSettings().value (QStringLiteral ("batch/saved")).toString(),
                                                    tr ("PHINYX batches (*.phbatch)"));
    if (path.isEmpty())
        return;
    QFile file (path);
    if (! file.open (QIODevice::ReadOnly))
    {
        QMessageBox::warning (this, tr ("Open Batch"), file.errorString());
        return;
    }
    std::string error;
    auto loaded = batch::fromJson (file.readAll().toStdString(), error);
    if (! error.empty())
    {
        QMessageBox::warning (this, tr ("Open Batch"), tr ("%1: %2").arg (QFileInfo (path).fileName(), qs (error)));
        return;
    }
    QSettings().setValue (QStringLiteral ("batch/saved"), path);
    job = std::move (loaded);
    filling = true;
    modeBox->setCurrentIndex (std::max (0, modeBox->findData ((int) job.mode)));
    tagTemplate->setPlainText (qs (job.tagTemplate));
    renamePattern->setText (qs (job.renamePattern));
    reportBox->setChecked (! job.reportPath.empty());
    reportEdit->setText (qs (job.reportPath));
    threadsBox->setValue (job.threads);
    filling = false;
    refreshFiles();
    refreshSteps();
    refreshOutputs();
    refreshMode();
    setWindowTitle (tr ("Batch Processor: %1").arg (QFileInfo (path).completeBaseName()));
}

void BatchWindow::saveBatch()
{
    auto path = QFileDialog::getSaveFileName (this, tr ("Save Batch"), QSettings().value (QStringLiteral ("batch/saved"), QDir::homePath() + tr ("/Batch.phbatch")).toString(),
                                              tr ("PHINYX batches (*.phbatch)"));
    if (path.isEmpty())
        return;
    if (! path.endsWith (QStringLiteral (".phbatch")))
        path += QStringLiteral (".phbatch");
    auto copy = job;
    copy.reportPath = reportBox->isChecked() ? reportEdit->text().toStdString() : std::string();
    QFile file (path);
    if (! file.open (QIODevice::WriteOnly | QIODevice::Truncate) || file.write (QByteArray::fromStdString (batch::toJson (copy))) < 0)
    {
        QMessageBox::warning (this, tr ("Save Batch"), file.errorString());
        return;
    }
    QSettings().setValue (QStringLiteral ("batch/saved"), path);
    setWindowTitle (tr ("Batch Processor: %1").arg (QFileInfo (path).completeBaseName()));
    status->setText (tr ("Saved %1").arg (QFileInfo (path).fileName()));
}

QString BatchWindow::reportPath() const
{
    if (! reportEdit->text().isEmpty())
        return reportEdit->text();
    const auto folder = job.files.empty() ? QDir::homePath() : QFileInfo (qs (job.files.front())).absolutePath();
    return folder + tr ("/PHINYX report %1.csv").arg (QDateTime::currentDateTime().toString (QStringLiteral ("yyyy-MM-dd HH.mm")));
}

//==============================================================================
void BatchWindow::start()
{
    if (running)
        return;
    if (job.files.empty())
    {
        status->setText (tr ("Add some files first"));
        return;
    }
    if (job.mode == batch::Mode::render && job.outputs.empty() && job.steps.empty())
    {
        status->setText (tr ("Nothing to do: add a step or an output"));
        return;
    }
    if (job.mode == batch::Mode::render && job.outputs.empty()
        && QMessageBox::question (this, tr ("Batch Processor"), tr ("There are no outputs, so the processed audio is not written anywhere. "
                                                                    "Measure only?")) != QMessageBox::Yes)
        return;
    if (job.mode == batch::Mode::tagsOnly && job.tagTemplate.empty())
    {
        status->setText (tr ("Type the tags to write first"));
        return;
    }
    if (job.mode == batch::Mode::rename
        && QMessageBox::question (this, tr ("Rename Files"), tr ("Rename %n file(s) from their tags with \"%1\"? There is no undo for this.", nullptr,
                                                                 (int) job.files.size()).arg (qs (job.renamePattern))) != QMessageBox::Yes)
        return;

    for (auto& w : workers)
        if (w.joinable())
            w.join();
    workers.clear();

    run = std::make_shared<Run> (job.files.size());
    run->job = job;
    if (reportBox->isChecked() || job.mode == batch::Mode::report)
        reportEdit->setText (reportPath());
    const auto threads = std::clamp<size_t> (job.threads > 0 ? (size_t) job.threads : std::max (1u, std::thread::hardware_concurrency()), 1, job.files.size());

    results->clear();
    for (const auto& f : job.files)
    {
        auto* item = new QTreeWidgetItem (results, { QFileInfo (qs (f)).fileName(), tr ("Waiting") });
        item->setData (0, Qt::UserRole, qs (f));
    }

    running = true;
    runButton->setText (tr ("&Cancel"));
    openReportButton->setEnabled (false);
    progress->setValue (0);
    status->setText (tr ("Running %n at once", nullptr, (int) threads));
    for (auto* w : { static_cast<QWidget*> (modeBox), static_cast<QWidget*> (fileList), static_cast<QWidget*> (stepsArea),
                     static_cast<QWidget*> (outputsArea), static_cast<QWidget*> (tagsArea), static_cast<QWidget*> (renameArea) })
        w->setEnabled (false);

    run->workersLeft = (int) threads;
    auto master = masterRender;
    QPointer<BatchWindow> self (this);
    for (size_t t = 0; t < threads; ++t)
        workers.emplace_back ([r = run, master, self]
        {
            for (;;)
            {
                const auto i = r->next++;
                if (i >= r->job.files.size() || r->cancel.load())
                    break;
                QMetaObject::invokeMethod (self, [self, i] { if (self != nullptr && self->results->topLevelItem ((int) i) != nullptr) self->results->topLevelItem ((int) i)->setText (1, tr ("Working...")); }, Qt::QueuedConnection);
                auto result = batch::runFile (r->job.files[i], r->job, master, &r->progress[i], &r->cancel);
                r->results[i] = result;
                r->progress[i] = 1.0f;
                ++r->done;
                QMetaObject::invokeMethod (self, [self, i, result]
                {
                    if (self == nullptr)
                        return;
                    auto* item = self->results->topLevelItem ((int) i);
                    if (item == nullptr)
                        return;
                    item->setText (1, result.ok ? tr ("Done") : qs (result.error));
                    if (! result.ok)
                        item->setForeground (1, QColor (0xef, 0x53, 0x50));
                    if (result.before.truePeakDb > -199.0)
                        item->setText (2, tr ("%1 LUFS, %2 dBTP").arg (db (result.before.integratedLufs), db (result.before.truePeakDb)));
                    if (result.after.truePeakDb > -199.0)
                        item->setText (3, tr ("%1 LUFS, %2 dBTP").arg (db (result.after.integratedLufs), db (result.after.truePeakDb)));
                    QStringList written, notes;
                    for (const auto& o : result.outputs)
                    {
                        written << (o.skipped ? tr ("skipped %1") : QStringLiteral ("%1")).arg (QFileInfo (qs (o.path)).fileName());
                        if (! o.warning.empty())
                            notes << qs (o.warning);
                    }
                    if (! result.renamedTo.empty())
                    {
                        written << QFileInfo (qs (result.renamedTo)).fileName();
                        item->setData (0, Qt::UserRole, qs (result.renamedTo));
                    }
                    else if (! result.outputs.empty() && result.outputs.front().ok)
                        item->setData (0, Qt::UserRole, qs (result.outputs.front().path));
                    for (const auto& l : result.log)
                        notes << qs (l);
                    if (result.clippedStretches > 0)
                        notes << tr ("%n clipped stretch(es)", nullptr, result.clippedStretches);
                    item->setText (4, written.join (QStringLiteral (", ")));
                    item->setText (5, notes.join (QStringLiteral ("; ")));
                    item->setToolTip (5, notes.join (QLatin1Char ('\n')));
                }, Qt::QueuedConnection);
            }
            --r->workersLeft;
        });
    poll->start();
}

void BatchWindow::stop()
{
    if (run != nullptr)
        run->cancel = true;
    status->setText (tr ("Stopping after the files in progress..."));
}

void BatchWindow::finish()
{
    poll->stop();
    for (auto& w : workers)
        if (w.joinable())
            w.join();
    workers.clear();
    running = false;
    runButton->setText (tr ("&Run"));
    for (auto* w : { static_cast<QWidget*> (modeBox), static_cast<QWidget*> (fileList), static_cast<QWidget*> (stepsArea),
                     static_cast<QWidget*> (outputsArea), static_cast<QWidget*> (tagsArea), static_cast<QWidget*> (renameArea) })
        w->setEnabled (true);

    int ok = 0, failed = 0;
    std::vector<batch::FileResult> finished;
    for (size_t i = 0; i < run->results.size(); ++i)
    {
        if (run->results[i].path.empty())
            continue;   // never started (cancelled)
        (run->results[i].ok ? ok : failed)++;
        finished.push_back (run->results[i]);
    }
    progress->setValue (run->cancel ? progress->value() : 1000);
    for (int c = 0; c < results->columnCount() - 1; ++c)
        results->resizeColumnToContents (c);

    // Renamed files keep their new names in the list.
    if (job.mode == batch::Mode::rename)
    {
        for (const auto& r : finished)
            if (! r.renamedTo.empty())
                std::replace (job.files.begin(), job.files.end(), r.path, r.renamedTo);
        refreshFiles();
    }

    QString text = run->cancel ? tr ("Cancelled: %1 done").arg (ok) : tr ("Finished: %n done", nullptr, ok);
    if (failed > 0)
        text += tr (", %n with a problem", nullptr, failed);
    if (reportBox->isChecked() || job.mode == batch::Mode::report)
    {
        std::ofstream out (reportPath().toStdString(), std::ios::binary);
        out << batch::reportCsv (finished);
        if (out)
        {
            openReportButton->setEnabled (true);
            text += tr (". Report: %1").arg (QFileInfo (reportPath()).fileName());
        }
        else
            text += tr (". Could not write the report to %1").arg (reportPath());
    }
    status->setText (text);
    run.reset();
    QApplication::beep();
}

void BatchWindow::closeEvent (QCloseEvent* e)
{
    // Closing hides the window; a running batch carries on.
    if (running)
        status->setText (tr ("Still running in the background"));
    remember();
    e->accept();
}
