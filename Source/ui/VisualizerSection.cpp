// SPDX-License-Identifier: AGPL-3.0-only
// The Visualizer (View menu): its window, full screen, and behind the PHINYX window.
#include "MainWindow.h"
#include "Settings.h"
#include "Theme.h"
#include "Visualizer.h"
#include "WaveformView.h"

#if PHINYX_HAVE_PROJECTM
 #include "MilkDrop.h"
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

void MainWindow::registerVisualizerCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("View");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    visualMode = std::max (0, VisualizerView::modeIndex (appSettings().value (QStringLiteral ("visualizer/mode")).toString()));
    VisualizerView::setRainTempo (appSettings().value (QStringLiteral ("visualizer/rainTempo"), 120.0).toDouble());

    add ("view.visualizer", tr ("&Visualizer"), { KS (Qt::SHIFT | Qt::Key_F11) }, [this]
    {
        auto* w = visualizerWindow();
        if (w->isVisible())
            w->close();
        else
        {
            w->show();
            w->raise();
            w->activateWindow();
            w->view()->setFocus();
        }
    })->setStatusTip (tr ("Visualizer: spectrum bars, radial, starfield, spectrum analyzer, oscilloscope, spectrogram, waterfall, music visualization"));
    add ("view.visualizerFullScreen", tr ("Visualizer &Full Screen"), { KS (Qt::Key_F11) }, [this]
    {
        auto* w = visualizerWindow();
        if (! w->isVisible())
        {
            w->closeWhenLeavingFullScreen = true;
            w->show();
        }
        w->toggleFullScreen();
    })->setStatusTip (tr ("The visualizer on the whole screen; Esc goes back"));
    auto* behind = add ("view.visualizerBehind", tr ("Visualizer &Behind PHINYX"), { KS (Qt::CTRL | Qt::Key_F11) }, [this] { toggleVisualizerBehind(); });
    behind->setCheckable (true);
    behind->setStatusTip (tr ("The visualizer as the background of the whole PHINYX window, seen through it as far as the window opacity allows"));
    add ("view.visualizerNext", tr ("&Next Visualization"), { KS (Qt::ALT | Qt::Key_F11) }, [this] { setVisualMode (visualMode + 1, true); });
    add ("view.visualizerPrevious", tr ("&Previous Visualization"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_F11) },
         [this] { setVisualMode (visualMode - 1, true); });
    add ("view.visualizerChoose", tr ("&Choose Visualization..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_F11) }, [this]
    {
        const auto& list = VisualizerView::modes();
        QStringList items;
        for (const auto& m : list)
            items << QStringLiteral ("%1 (%2): %3").arg (VisualizerView::tr (m.name), m.science ? tr ("science") : tr ("artistic"),
                                                         VisualizerView::tr (m.about));
        bool ok = false;
        const auto chosen = QInputDialog::getItem (this, tr ("Choose Visualization"), tr ("Visualization:"), items, visualMode, false, &ok);
        if (ok)
            setVisualMode ((int) items.indexOf (chosen), true);
        else
            waveform->setFocus();
    });

    // Digital Rain's tempo, for when keys can't reach the view (behind PHINYX).
    add ("view.rainTempo", tr ("Digital &Rain Tempo..."), {}, [this]
    {
        const auto tagged = fileTempo();
        bool ok = false;
        const auto bpm = QInputDialog::getInt (this, tr ("Digital Rain Tempo"),
                                               tagged > 0.0 ? tr ("Beats per minute, 0 (still) to 240. This file's tag says %1 BPM.").arg (tagged)
                                                            : tr ("Beats per minute, 0 (still) to 240:"),
                                               (int) std::lround (VisualizerView::rainTempo()), 0, (int) VisualizerView::maxRainTempo, 1, &ok);
        if (ok)
        {
            setRainTempo (bpm);
            const int rain = VisualizerView::modeIndex (QStringLiteral ("rain"));
            if (visualMode != rain)
                setVisualMode (rain, true);
        }
        waveform->setFocus();
    })->setStatusTip (tr ("How fast the Digital Rain visualizer falls: 0 (still) to 240 BPM"));
    auto* follow = add ("view.rainFollowsFile", tr ("Digital Rain Follows the File's Tempo"), {}, [this]
    {
        const bool on = commands.action (QStringLiteral ("view.rainFollowsFile"))->isChecked();
        appSettings().setValue (QStringLiteral ("visualizer/rainFollowsFile"), on);
        const auto tagged = fileTempo();
        if (on && tagged > 0.0)
            setRainTempo (tagged);
        statusBar()->showMessage (! on ? tr ("Digital Rain keeps its own tempo")
                                  : tagged > 0.0 ? tr ("Digital Rain follows this file's %1 BPM").arg (tagged)
                                                 : tr ("This file has no tempo tag yet: Analyze > Tempo and Key finds and writes one"), 6000);
    });
    follow->setCheckable (true);
    follow->setChecked (appSettings().value (QStringLiteral ("visualizer/rainFollowsFile"), false).toBool());
    follow->setStatusTip (tr ("Digital Rain takes its tempo from the open file's BPM tag (Analyze > Tempo and Key writes one)"));

#if PHINYX_HAVE_PROJECTM
    // MilkDrop presets: these act on the visualizer showing (the window first), switching it to MilkDrop.
    add ("view.milkdropNext", tr ("Next &MilkDrop Preset"), { KS (Qt::ALT | Qt::Key_F12) }, [this]
    {
        if (auto* view = showMilkDrop())
        {
            view->milkDropStep (1);
            statusBar()->showMessage (tr ("MilkDrop: next preset"), 2000);
        }
    });
    add ("view.milkdropPrevious", tr ("Previous MilkDrop Preset"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_F12) }, [this]
    {
        if (auto* view = showMilkDrop())
            view->milkDropStep (-1);
    });
    add ("view.milkdropChoose", tr ("Choose MilkDrop &Preset..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_F12) }, [this] { chooseMilkDropPreset(); })
        ->setStatusTip (tr ("Pick a MilkDrop preset by name or category: fractal, particles, geometric, hypnotic, waveform"));
    add ("view.milkdropDownload", tr ("&Download MilkDrop Presets..."), {}, [this] { downloadMilkDropPresets(); })
        ->setStatusTip (tr ("Download the free Cream of the Crop collection of MilkDrop presets for the visualizer"));
#endif

    // Each view on its own, so Find lists them all.
    const auto& list = VisualizerView::modes();
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto id = QStringLiteral ("visualizer.%1").arg (QString::fromLatin1 (list[i].id));
        auto* action = commands.add (id, category, tr ("Visualizer: %1").arg (VisualizerView::tr (list[i].name)), {},
                                     [this, i] { setVisualMode ((int) i, true); });
        action->setStatusTip (VisualizerView::tr (list[i].about) + (list[i].science ? tr (" (science)") : tr (" (artistic)")));
        addAction (action);
    }
}

VisualizerWindow* MainWindow::visualizerWindow()
{
    if (visualizer == nullptr)
    {
        visualizer = new VisualizerWindow (this);
        visualizer->view()->setMode (visualMode);
        visualizer->syncPicker();
        connect (visualizer, &VisualizerWindow::modeChosen, this, [this] (int index) { setVisualMode (index, false); });
        connect (visualizer->view(), &VisualizerView::stepMode, this, [this] (int step) { setVisualMode (visualMode + step, false); });
        connect (visualizer->view(), &VisualizerView::rainTempoRequested, this, [this] (double bpm) { setRainTempo (bpm); });
        connect (visualizer, &VisualizerWindow::backdropRequested, this, [this] { toggleVisualizerBehind(); });
        connect (visualizer, &VisualizerWindow::closed, this, [this]
        {
            activateWindow();
            waveform->setFocus();
        });
    }
    return visualizer;
}

// Shows a view; if none is showing, opens the window so the choice is seen.
void MainWindow::setVisualMode (int index, bool openIfHidden)
{
    const int count = (int) VisualizerView::modes().size();
    visualMode = ((index % count) + count) % count;
    appSettings().setValue (QStringLiteral ("visualizer/mode"), QString::fromLatin1 (VisualizerView::modes()[(size_t) visualMode].id));
    if (visualizer != nullptr)
    {
        visualizer->view()->setMode (visualMode);
        visualizer->syncPicker();
    }
    if (visualBackdrop != nullptr)
        visualBackdrop->setMode (visualMode);

    const bool showing = (visualizer != nullptr && visualizer->isVisible()) || (visualBackdrop != nullptr && visualBackdrop->isVisible());
    if (openIfHidden && ! showing)
    {
        auto* w = visualizerWindow();
        w->show();
        w->raise();
        w->activateWindow();
        w->view()->setFocus();
    }
    statusBar()->showMessage (tr ("Visualizer: %1").arg (VisualizerView::tr (VisualizerView::modes()[(size_t) visualMode].name)), 2500);
}

void MainWindow::toggleVisualizerBehind()
{
    auto* action = commands.action (QStringLiteral ("view.visualizerBehind"));
    if (visualBackdrop != nullptr && visualBackdrop->isVisible())
    {
        visualBackdrop->hide();
        action->setChecked (false);
        if (opacityBeforeBackdrop > 0.0)
            changeOpacity (opacityBeforeBackdrop, false);
        opacityBeforeBackdrop = 0.0;
        statusBar()->showMessage (tr ("Visualizer behind PHINYX off"), 3000);
        return;
    }

    if (visualBackdrop == nullptr)
        visualBackdrop = new VisualizerView (this, true);
    visualBackdrop->setMode (visualMode);
    visualBackdrop->setGeometry (rect());
    visualBackdrop->show();
    visualBackdrop->lower();
    action->setChecked (true);

    // A fully opaque window would hide it: see through the window by default.
    opacityBeforeBackdrop = 0.0;
    if (Theme::opacity() > 0.8)
    {
        opacityBeforeBackdrop = Theme::opacity();
        changeOpacity (0.55, false);
    }
    statusBar()->showMessage (tr ("Visualizer behind PHINYX: Ctrl+Alt+- and Ctrl+Alt+= (or the Opacity slider) show more or less of it; Alt+F11 changes the view"), 8000);
}

void MainWindow::setRainTempo (double bpm)
{
    VisualizerView::setRainTempo (bpm);
    appSettings().setValue (QStringLiteral ("visualizer/rainTempo"), VisualizerView::rainTempo());
    statusBar()->showMessage (VisualizerView::rainTempo() > 0.0 ? tr ("Digital Rain: %1 BPM").arg (std::lround (VisualizerView::rainTempo()))
                                                                : tr ("Digital Rain: still"), 2500);
}

double MainWindow::fileTempo() const
{
    const auto& tags = doc.current().tags;
    const auto it = tags.find (tag::bpm);
    if (it == tags.end())
        return 0.0;
    auto bpm = QString::fromStdString (it->second).toDouble();
    while (bpm > VisualizerView::maxRainTempo)   // 280 BPM falls at 140
        bpm /= 2.0;
    return std::max (0.0, std::round (bpm));
}

void MainWindow::feedVisualizers()
{
    if (commands.action (QStringLiteral ("view.rainFollowsFile"))->isChecked())
        if (const auto tagged = fileTempo(); tagged > 0.0 && std::abs (tagged - VisualizerView::rainTempo()) > 0.01)
            setRainTempo (tagged);

    const auto rate = engine.deviceSampleRate();
    for (auto* view : { visualizer != nullptr && visualizer->isVisible() ? visualizer->view() : nullptr,
                        visualBackdrop != nullptr && visualBackdrop->isVisible() ? visualBackdrop : nullptr })
        if (view != nullptr)
        {
            view->addSamples (scopeLeft, scopeRight, rate);
            view->tick();
        }
}

#if PHINYX_HAVE_PROJECTM
namespace
{
    // Up, Down, Page Up and Page Down in a search box move through its list.
    class ListKeys final : public QObject
    {
    public:
        ListKeys (QListWidget* l, QObject* parent) : QObject (parent), list (l) {}

        bool eventFilter (QObject*, QEvent* e) override
        {
            if (e->type() != QEvent::KeyPress)
                return false;
            const auto key = static_cast<QKeyEvent*> (e)->key();
            if (key != Qt::Key_Up && key != Qt::Key_Down && key != Qt::Key_PageUp && key != Qt::Key_PageDown)
                return false;
            QKeyEvent copy (QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent (list, &copy);
            return true;
        }

    private:
        QListWidget* list;
    };
}

// The visualizer that is showing (the window first, else the one behind PHINYX), switched to MilkDrop.
// If none is showing, the window opens.
VisualizerView* MainWindow::showMilkDrop()
{
    const int milkdrop = VisualizerView::modeIndex (QStringLiteral ("milkdrop"));
    if (visualMode != milkdrop)
        setVisualMode (milkdrop, true);
    if (visualizer != nullptr && visualizer->isVisible())
        return visualizer->view();
    if (visualBackdrop != nullptr && visualBackdrop->isVisible())
        return visualBackdrop;
    setVisualMode (milkdrop, true);
    return visualizer != nullptr ? visualizer->view() : nullptr;
}

void MainWindow::chooseMilkDropPreset()
{
    auto* view = showMilkDrop();
    auto* milk = view != nullptr ? view->milkDrop() : nullptr;
    if (milk == nullptr)
        return;
    const auto all = milk->presets();
    if (all.isEmpty())
    {
        downloadMilkDropPresets();
        return;
    }

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("MilkDrop Presets"));
    auto* filter = new QLineEdit (&dialog);
    filter->setPlaceholderText (tr ("Type to search by name or category (Fractal, Particles, Waveform...)"));
    auto* list = new QListWidget (&dialog);
    for (int i = 0; i < all.size(); ++i)
    {
        const QFileInfo info (all[i]);
        const auto category = MilkDrop::categoryOf (all[i]);
        auto* item = new QListWidgetItem (category.isEmpty() ? info.completeBaseName()
                                                             : QStringLiteral ("%1  /  %2").arg (category, info.completeBaseName()), list);
        item->setData (Qt::UserRole, i);
    }
    list->setCurrentRow (milk->position());
    connect (filter, &QLineEdit::textChanged, &dialog, [list] (const QString& text)
    {
        const auto words = text.split (QLatin1Char (' '), Qt::SkipEmptyParts);
        QListWidgetItem* first = nullptr;
        for (int i = 0; i < list->count(); ++i)
        {
            auto* item = list->item (i);
            const bool match = std::all_of (words.begin(), words.end(), [item] (const QString& w) { return item->text().contains (w, Qt::CaseInsensitive); });
            item->setHidden (! match);
            if (match && first == nullptr)
                first = item;
        }
        if (first != nullptr)
            list->setCurrentItem (first);
    });
    // Moving through the list tries each preset at once; Enter keeps it, Esc goes back.
    const int before = milk->position();
    connect (list, &QListWidget::currentItemChanged, &dialog, [view] (QListWidgetItem* item)
    {
        if (item != nullptr)
            view->milkDropChoose (item->data (Qt::UserRole).toInt());
    });
    connect (list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    filter->installEventFilter (new ListKeys (list, filter));
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout (&dialog);
    layout->addWidget (new QLabel (tr ("%1 presets. Moving through the list shows each one; Enter keeps it.").arg (all.size()), &dialog));
    layout->addWidget (filter);
    layout->addWidget (list, 1);
    layout->addWidget (buttons);
    dialog.resize (560, 600);
    // Up and Down in the search box move through the list.
    connect (filter, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    filter->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        view->milkDropChoose (before);
}

void MainWindow::downloadMilkDropPresets()
{
    const auto folder = MilkDrop::userPresetFolder();
    const auto presets = folder + QStringLiteral ("/cream-of-the-crop");
    if (QFileInfo (presets).isDir())
    {
        QMessageBox::information (this, tr ("MilkDrop Presets"), tr ("The Cream of the Crop presets are already in %1.").arg (presets));
        return;
    }
    if (QStandardPaths::findExecutable (QStringLiteral ("git")).isEmpty())
    {
        QMessageBox::warning (this, tr ("MilkDrop Presets"), tr ("Downloading needs git. Install it with:\n\n    sudo pacman -S git"));
        return;
    }
    if (QMessageBox::question (this, tr ("MilkDrop Presets"),
                               tr ("Download the free \"Cream of the Crop\" MilkDrop collection (9,795 presets curated by ISOSCELES, "
                                   "about 70 MB) and the MilkDrop texture pack from the projectM project on GitHub into %1?").arg (folder))
        != QMessageBox::Yes)
        return;

    QDir().mkpath (folder);
    QProgressDialog progress (tr ("Downloading MilkDrop presets..."), tr ("Cancel"), 0, 0, this);
    progress.setWindowModality (Qt::WindowModal);
    progress.setMinimumDuration (0);
    progress.show();

    const auto clone = [&] (const QString& url, const QString& into)
    {
        QProcess git;
        git.start (QStringLiteral ("git"), { QStringLiteral ("clone"), QStringLiteral ("--depth"), QStringLiteral ("1"), url, into });
        while (! git.waitForFinished (100))
        {
            QCoreApplication::processEvents();
            if (progress.wasCanceled())
            {
                git.kill();
                git.waitForFinished();
                QDir (into).removeRecursively();
                return false;
            }
        }
        if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0)
        {
            QMessageBox::warning (this, tr ("MilkDrop Presets"), tr ("The download failed:\n%1").arg (QString::fromLocal8Bit (git.readAllStandardError()).trimmed()));
            QDir (into).removeRecursively();
            return false;
        }
        QDir (into + QStringLiteral ("/.git")).removeRecursively();   // only the files are needed
        return true;
    };
    const auto partial = folder + QStringLiteral ("/.cream-of-the-crop-download");
    QDir (partial).removeRecursively();
    if (! clone (QStringLiteral ("https://github.com/projectM-visualizer/presets-cream-of-the-crop.git"), partial))
        return;
    QDir().rename (partial, presets);
    if (! QFileInfo (folder + QStringLiteral ("/textures")).isDir())
    {
        const auto pack = folder + QStringLiteral ("/.texture-pack");
        QDir (pack).removeRecursively();
        if (clone (QStringLiteral ("https://github.com/projectM-visualizer/presets-milkdrop-texture-pack.git"), pack))
        {
            QDir().rename (pack + QStringLiteral ("/textures"), folder + QStringLiteral ("/textures"));
            QDir (pack).removeRecursively();
        }
    }
    progress.close();

    for (auto* view : { visualizer != nullptr ? visualizer->view() : nullptr, visualBackdrop })
        if (view != nullptr)
            view->reloadMilkDrop();
    statusBar()->showMessage (tr ("MilkDrop presets downloaded to %1").arg (folder), 6000);
    showMilkDrop();
}
#endif
