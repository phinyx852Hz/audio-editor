// SPDX-License-Identifier: AGPL-3.0-only
// The Visualizer (View menu): its window, full screen, and behind the PHINYX window.
#include "MainWindow.h"
#include "Settings.h"
#include "Theme.h"
#include "Visualizer.h"
#include "WaveformView.h"

#include <QInputDialog>
#include <QStatusBar>

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

void MainWindow::feedVisualizers()
{
    const auto rate = engine.deviceSampleRate();
    for (auto* view : { visualizer != nullptr && visualizer->isVisible() ? visualizer->view() : nullptr,
                        visualBackdrop != nullptr && visualBackdrop->isVisible() ? visualBackdrop : nullptr })
        if (view != nullptr)
        {
            view->addSamples (scopeLeft, scopeRight, rate);
            view->tick();
        }
}
