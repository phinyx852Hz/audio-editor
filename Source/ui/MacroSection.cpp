// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Macros: record, run and manage macros (see Macros.h).
#include "MainWindow.h"
#include "Macros.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
    // Shows and edits the saved macros: run, rename and delete them, and add,
    // remove and reorder their steps. Every change is saved at once.
    class MacroManager final : public QDialog
    {
    public:
        MacroManager (CommandRegistry& c, std::function<void (const macro::Macro&)> runIt, std::function<void()> recordIt, QWidget* parent)
            : QDialog (parent), commands (c), run (std::move (runIt)), record (std::move (recordIt))
        {
            setWindowTitle (tr ("Macros"));
            setProperty ("macroSkip", true);
            resize (720, 420);

            macros = new QListWidget;
            macros->setAccessibleName (tr ("Macros"));
            steps = new QListWidget;
            steps->setAccessibleName (tr ("Steps"));
            ask = new QCheckBox (tr ("&Ask for this step's settings each time"));
            ask->setToolTip (tr ("Off: the step runs with the settings it was recorded with, without showing its window"));

            auto* left = new QVBoxLayout;
            auto* macrosLabel = new QLabel (tr ("&Macros:"));
            macrosLabel->setBuddy (macros);
            left->addWidget (macrosLabel);
            left->addWidget (macros, 1);
            auto* macroButtons = new QHBoxLayout;
            auto* runButton = button (tr ("&Run"), macroButtons, [this] { runSelected(); });
            button (tr ("Record &New..."), macroButtons, [this] { accept(); record(); });
            button (tr ("Re&name..."), macroButtons, [this] { renameSelected(); });
            button (tr ("&Delete"), macroButtons, [this] { deleteSelected(); });
            left->addLayout (macroButtons);

            auto* right = new QVBoxLayout;
            auto* stepsLabel = new QLabel (tr ("S&teps:"));
            stepsLabel->setBuddy (steps);
            right->addWidget (stepsLabel);
            right->addWidget (steps, 1);
            right->addWidget (ask);
            auto* stepButtons = new QHBoxLayout;
            button (tr ("Add &Step..."), stepButtons, [this] { addStep(); });
            button (tr ("Re&move Step"), stepButtons, [this] { editSteps ([this] (QList<macro::Step>& s, int i) { s.removeAt (i); return std::min (i, (int) s.size() - 1); }); });
            button (tr ("Move &Up"), stepButtons, [this] { editSteps ([] (QList<macro::Step>& s, int i) { if (i > 0) s.swapItemsAt (i, i - 1); return std::max (0, i - 1); }); });
            button (tr ("Move Do&wn"), stepButtons, [this] { editSteps ([] (QList<macro::Step>& s, int i) { if (i + 1 < s.size()) s.swapItemsAt (i, i + 1); return std::min ((int) s.size() - 1, i + 1); }); });
            right->addLayout (stepButtons);

            auto* columns = new QHBoxLayout;
            columns->addLayout (left, 2);
            columns->addLayout (right, 3);
            auto* close = new QDialogButtonBox (QDialogButtonBox::Close);
            connect (close, &QDialogButtonBox::rejected, this, &QDialog::reject);
            auto* layout = new QVBoxLayout (this);
            layout->addLayout (columns, 1);
            layout->addWidget (close);

            connect (macros, &QListWidget::currentRowChanged, this, [this] { showSteps(); });
            connect (macros, &QListWidget::itemActivated, this, [this] { runSelected(); });
            connect (steps, &QListWidget::currentRowChanged, this, [this] { showStep(); });
            connect (ask, &QCheckBox::toggled, this, [this] (bool on)
            {
                editSteps ([on] (QList<macro::Step>& s, int i) { s[i].ask = on; return i; });
            });
            runButton->setDefault (true);
            reload();
        }

    private:
        QPushButton* button (const QString& text, QHBoxLayout* row, std::function<void()> fn)
        {
            auto* b = new QPushButton (text);
            b->setAutoDefault (false);
            connect (b, &QPushButton::clicked, this, std::move (fn));
            row->addWidget (b);
            return b;
        }

        QString commandName (const QString& id) const
        {
            if (auto* a = commands.action (id))
                return a->text().remove (QLatin1Char ('&')).remove (QStringLiteral ("..."));
            return tr ("%1 (not available)").arg (id);
        }

        void reload (const QString& select = {})
        {
            const auto keep = select.isEmpty() && macros->currentItem() != nullptr ? macros->currentItem()->text() : select;
            all = macro::loadAll();
            const QSignalBlocker block (macros);
            macros->clear();
            for (const auto& m : all)
                macros->addItem (m.name);
            const auto found = macros->findItems (keep, Qt::MatchExactly);
            macros->setCurrentRow (found.isEmpty() ? 0 : macros->row (found.first()));
            showSteps();
        }

        macro::Macro* selected()
        {
            const auto row = macros->currentRow();
            return row >= 0 && row < all.size() ? &all[row] : nullptr;
        }

        void showSteps (int select = 0)
        {
            const QSignalBlocker block (steps);
            steps->clear();
            if (auto* m = selected())
                for (int i = 0; i < m->steps.size(); ++i)
                {
                    const auto& s = m->steps[i];
                    auto text = tr ("%1. %2").arg (i + 1).arg (commandName (s.command));
                    if (s.ask)
                        text += tr ("   (asks for settings)");
                    else if (! s.dialogs.isEmpty())
                        text += tr ("   (with its recorded settings)");
                    steps->addItem (text);
                }
            steps->setCurrentRow (std::min (select, steps->count() - 1));
            showStep();
        }

        void showStep()
        {
            const auto* m = selected();
            const auto row = steps->currentRow();
            const QSignalBlocker block (ask);
            ask->setEnabled (m != nullptr && row >= 0);
            ask->setChecked (m != nullptr && row >= 0 && m->steps[row].ask);
        }

        void editSteps (const std::function<int (QList<macro::Step>&, int)>& change)
        {
            auto* m = selected();
            const auto row = steps->currentRow();
            if (m == nullptr || row < 0 || row >= m->steps.size())
                return;
            const auto next = change (m->steps, row);
            QString error;
            if (! macro::save (*m, error))
                QMessageBox::warning (this, tr ("Macros"), error);
            showSteps (next);
            steps->setFocus();
        }

        void addStep()
        {
            auto* m = selected();
            if (m == nullptr)
                return;
            QStringList names;
            QStringList ids;
            for (const auto& c : commands.all())
                if (macro::recordable (c.id))
                {
                    names << tr ("%1: %2").arg (c.category, c.name());
                    ids << c.id;
                }
            bool ok = false;
            const auto choice = QInputDialog::getItem (this, tr ("Add Step"), tr ("Command:"), names, 0, false, &ok);
            if (! ok)
                return;
            const auto at = steps->currentRow() + 1;
            m->steps.insert (at, { ids[names.indexOf (choice)], true, {} });   // no settings recorded yet: it asks
            QString error;
            if (! macro::save (*m, error))
                QMessageBox::warning (this, tr ("Macros"), error);
            showSteps (at);
        }

        void runSelected()
        {
            if (const auto* m = selected())
            {
                const auto copy = *m;
                accept();
                run (copy);
            }
        }

        void renameSelected()
        {
            const auto* m = selected();
            if (m == nullptr)
                return;
            bool ok = false;
            const auto name = QInputDialog::getText (this, tr ("Rename Macro"), tr ("Name:"), QLineEdit::Normal, m->name, &ok).trimmed();
            if (! ok || name.isEmpty() || name == m->name)
                return;
            QString error;
            if (! macro::rename (m->name, name, error))
                QMessageBox::warning (this, tr ("Macros"), error);
            reload (name);
        }

        void deleteSelected()
        {
            const auto* m = selected();
            if (m == nullptr)
                return;
            if (QMessageBox::question (this, tr ("Delete Macro"), tr ("Delete the macro \"%1\"?").arg (m->name)) != QMessageBox::Yes)
                return;
            macro::remove (m->name);
            reload();
        }

        CommandRegistry& commands;
        std::function<void (const macro::Macro&)> run;
        std::function<void()> record;
        QList<macro::Macro> all;
        QListWidget* macros;
        QListWidget* steps;
        QCheckBox* ask;
    };
}

void MainWindow::registerMacroCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Tools");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* a = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (a);
        return a;
    };

    macroRecorder = new MacroRecorder (this);
    commands.setTransparent (QStringLiteral ("help.palette"));
    commands.setObserver ([this] (const QString& id, bool finished)
    {
        if (finished)
            macroRecorder->commandFinished (id);
        else
            macroRecorder->commandStarted (id);
    });

    macroStatus = new QLabel (this);
    macroStatus->setStyleSheet (QStringLiteral ("color: #ff5a5a; font-weight: 600; padding: 0 8px;"));
    macroStatus->hide();
    statusBar()->insertPermanentWidget (0, macroStatus);
    connect (macroRecorder, &MacroRecorder::recordingChanged, this, [this]
    {
        macroStatus->setVisible (macroRecorder->isRecording());
        macroStatus->setText (tr ("● Recording macro: %n step(s)", nullptr, macroRecorder->recordedSteps()));
        commands.action (QStringLiteral ("tools.macroRecord"))->setChecked (macroRecorder->isRecording());
    });

    add ("tools.macroRecord", tr ("&Record Macro"), { KS (Qt::CTRL | Qt::Key_F9) }, [this] { toggleMacroRecording(); })->setCheckable (true);
    add ("tools.macroApply", tr ("&Apply Macro..."), { KS (Qt::Key_F9) }, [this]
    {
        const auto all = macro::loadAll();
        if (all.isEmpty())
        {
            QMessageBox::information (this, tr ("Apply Macro"), tr ("There are no macros yet. Press %1 to start recording one, do the steps, then press it again.")
                                          .arg (commands.action (QStringLiteral ("tools.macroRecord"))->shortcut().toString (QKeySequence::NativeText)));
            return;
        }
        QStringList names;
        for (const auto& m : all)
            names << m.name;
        bool ok = false;
        const auto choice = QInputDialog::getItem (this, tr ("Apply Macro"), tr ("Macro to run on %1:").arg (hasSelection() ? tr ("the selection") : tr ("the file")),
                                                   names, std::max (0, (int) names.indexOf (lastMacro)), false, &ok);
        if (ok)
            runMacro (all[names.indexOf (choice)]);
    });
    add ("tools.macros", tr ("&Macros..."), { KS (Qt::SHIFT | Qt::Key_F9) }, [this]
    {
        MacroManager manager (commands, [this] (const macro::Macro& m) { QTimer::singleShot (0, this, [this, m] { runMacro (m); }); },
                              [this] { QTimer::singleShot (0, this, [this] { toggleMacroRecording(); }); }, this);
        manager.exec();
    });
}

void MainWindow::toggleMacroRecording()
{
    if (! macroRecorder->isRecording())
    {
        macroRecorder->startRecording();
        statusBar()->showMessage (tr ("Recording a macro: do the steps, then press %1 again to name and save it")
                                      .arg (commands.action (QStringLiteral ("tools.macroRecord"))->shortcut().toString (QKeySequence::NativeText)), 10000);
        return;
    }

    auto recorded = macroRecorder->stopRecording();
    if (recorded.steps.isEmpty())
    {
        statusBar()->showMessage (tr ("Stopped recording; nothing was recorded"), 6000);
        return;
    }
    for (;;)
    {
        bool ok = false;
        recorded.name = QInputDialog::getText (this, tr ("Save Macro"), tr ("%n step(s) recorded. Name for the macro:", nullptr, (int) recorded.steps.size()),
                                               QLineEdit::Normal, recorded.name, &ok).trimmed();
        if (! ok)
        {
            statusBar()->showMessage (tr ("The recorded macro was not saved"), 6000);
            return;
        }
        if (recorded.name.isEmpty())
            continue;
        bool exists = false;
        for (const auto& m : macro::loadAll())
            exists = exists || m.name == recorded.name;
        if (exists && QMessageBox::question (this, tr ("Save Macro"), tr ("Replace the macro \"%1\"?").arg (recorded.name)) != QMessageBox::Yes)
            continue;
        break;
    }
    QString error;
    if (! macro::save (recorded, error))
    {
        QMessageBox::warning (this, tr ("Save Macro"), error);
        return;
    }
    lastMacro = recorded.name;
    statusBar()->showMessage (tr ("Saved the macro \"%1\". Press %2 to run it")
                                  .arg (recorded.name, commands.action (QStringLiteral ("tools.macroApply"))->shortcut().toString (QKeySequence::NativeText)), 8000);
}

void MainWindow::runMacro (const macro::Macro& m)
{
    if (macroRecorder->isRunning())
        return;
    if (macroRecorder->isRecording())
    {
        statusBar()->showMessage (tr ("Stop recording before running a macro"), 6000);
        return;
    }
    lastMacro = m.name;

    // Waits while a file opens, so the next step finds it.
    const auto settle = [this]
    {
        for (int i = 0; loading && i < 6000; ++i)
        {
            QEventLoop wait;
            QTimer::singleShot (10, &wait, &QEventLoop::quit);
            wait.exec();
        }
    };

    int done = 0;
    for (const auto& step : m.steps)
    {
        auto* action = commands.action (step.command);
        const auto name = action != nullptr ? action->text().remove (QLatin1Char ('&')).remove (QStringLiteral ("...")) : step.command;
        if (action == nullptr || ! action->isEnabled())
        {
            QMessageBox::information (this, tr ("Apply Macro"),
                                      action == nullptr ? tr ("Step %1 of \"%2\" is a command this version doesn't have (%3). The macro stopped there.").arg (done + 1).arg (m.name, step.command)
                                                        : tr ("Step %1 of \"%2\", %3, can't run right now. The macro stopped there.").arg (done + 1).arg (m.name, name));
            break;
        }
        statusBar()->showMessage (tr ("Macro \"%1\": step %2 of %3, %4").arg (m.name).arg (done + 1).arg (m.steps.size()).arg (name));
        macroRecorder->beginStep (step);
        action->trigger();
        macroRecorder->endStep();
        settle();
        ++done;
    }
    if (done == m.steps.size())
        statusBar()->showMessage (tr ("Ran the macro \"%1\" (%n step(s))", nullptr, done).arg (m.name), 6000);
    waveform->setFocus();
}
