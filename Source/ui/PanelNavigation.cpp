// SPDX-License-Identifier: AGPL-3.0-only
// Getting around the panels with the keyboard: Alt tapped, F6 and Shift+F6 (or whatever keys
// Next and Previous Panel are given) go round them, Alt or the Next Panel key held with an
// arrow key goes to the one that way (as in a tiling window manager), Ctrl+W or Super+W
// closes only what has the keyboard, and Super+F takes a window full screen. Files dragged
// from the Find panel (or a file manager) onto the editor open there.
#include "MainWindow.h"
#include "AmbiScene.h"
#include "ChainRow.h"
#include "FindPanel.h"
#include "MasterPanel.h"
#include "MixerWindow.h"
#include "Panels.h"
#include "QuantizePanel.h"
#include "ReferencePanel.h"
#include "SamplerWindow.h"
#include "SlicerWindow.h"
#include "AlbumFile.h"

#include <QApplication>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <limits>

namespace
{
    // An application-wide event filter that hands every event to a function.
    class KeyFilter final : public QObject
    {
    public:
        KeyFilter (std::function<bool (QObject*, QEvent*)> f, QObject* parent) : QObject (parent), fn (std::move (f)) {}

    protected:
        bool eventFilter (QObject* o, QEvent* e) override { return fn (o, e); }

    private:
        std::function<bool (QObject*, QEvent*)> fn;
    };

    bool isArrow (int key)
    {
        return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    }

    bool holds (const QWidget* outer, const QWidget* w)
    {
        return outer != nullptr && w != nullptr && (w == outer || outer->isAncestorOf (w));
    }

    QStringList localFiles (const QMimeData* mime)
    {
        QStringList paths;
        if (mime != nullptr && mime->hasUrls())
            for (const auto& url : mime->urls())
                if (url.isLocalFile())
                    paths << url.toLocalFile();
        return paths;
    }
}

void MainWindow::buildNavigation()
{
    qApp->installEventFilter (new KeyFilter ([this] (QObject* o, QEvent* e) { return navigationKey (o, e); }, this));

    // Pointing at Simplest or the Slicer gives it the keyboard, like the other panels.
    auto areas = QList<QWidget*> { waveform, chainRow, masterWidget, markerList, historyList, diagnostics, findWidget, referencePanel,
                                   ambiSceneView, metersPanel->content(), samplerWin, slicerWin };
    focusFollowsMouse->setAreas (areas);

    setAcceptDrops (true);
}

std::vector<MainWindow::PanelStop> MainWindow::panelStops() const
{
    // The mixer in the editor's place stands in for the waveform.
    QWidget* editor = waveform;
    if (mixerDocked() && centralStack->currentWidget() == mixerWin)
        editor = mixerWin;
    return { { editor, nullptr }, { samplerWin, samplerPanel }, { chainRow, chainPanel }, { masterWidget, masterPanel },
             { metersPanel->content(), metersPanel }, { markerList, markerPanel }, { historyList, historyPanel },
             { diagnostics, diagnosticsPanel }, { referencePanel, referencesPanel }, { ambiSceneView, ambiScenePanel },
             { findWidget, findPanel }, { slicerWin, slicerPanel }, { quantizeWidget, quantizePanel } };
}

int MainWindow::stopWithFocus (const std::vector<PanelStop>& stops) const
{
    auto* focus = QApplication::focusWidget();
    for (int i = 0; i < (int) stops.size(); ++i)
    {
        const auto& s = stops[(size_t) i];
        if (s.panel != nullptr ? s.panel->containsFocus() : holds (s.widget, focus))
            return i;
    }
    return -1;
}

void MainWindow::focusStop (const PanelStop& next)
{
    if (openedByCycle != nullptr && openedByCycle != next.panel)
    {
        if (openedByCycle->isShown())
            openedByCycle->hide();
        openedByCycle = nullptr;
    }

    if (next.panel != nullptr)
    {
        if (! next.panel->isShown())
            openedByCycle = next.panel;
        next.panel->show (true);
        // The panel itself, or else the first thing in it that takes the keyboard.
        auto* target = next.widget;
        if (target != nullptr && target->focusPolicy() == Qt::NoFocus)
        {
            const auto children = target->findChildren<QWidget*>();
            const auto it = std::find_if (children.begin(), children.end(), [] (QWidget* c)
                                          { return (c->focusPolicy() & Qt::TabFocus) != 0 && c->isEnabled(); });
            target = it != children.end() ? *it : nullptr;
        }
        if (target != nullptr)
            QTimer::singleShot (0, target, [target]
            {
                if (target->isVisible())
                {
                    target->window()->activateWindow();
                    target->setFocus (Qt::TabFocusReason);
                }
            });
    }
    else
    {
        activateWindow();
        next.widget->setFocus (Qt::TabFocusReason);
    }
    const auto name = next.panel != nullptr ? next.panel->dock()->windowTitle() : (next.widget == mixerWin ? tr ("Mixer") : tr ("Waveform"));
    statusBar()->showMessage (name, 1500);
}

// F6 held with an arrow: the nearest showing panel on that side of the one with the keyboard,
// preferring one that lines up with it. Closed panels are left closed.
void MainWindow::focusDirection (int arrowKey)
{
    const auto stops = panelStops();
    int from = stopWithFocus (stops);
    if (from < 0)
        from = std::clamp (panelStop, 0, (int) stops.size() - 1);

    auto areaOf = [] (const PanelStop& s) -> QWidget* { return s.panel != nullptr ? s.panel->content() : s.widget; };
    auto rectOf = [] (QWidget* w) { return QRect (w->mapToGlobal (QPoint (0, 0)), w->size()); };
    const auto here = rectOf (areaOf (stops[(size_t) from]));
    constexpr int slack = 12;   // dock separators and frames between neighbours

    int best = -1;
    double bestScore = std::numeric_limits<double>::max();
    for (int i = 0; i < (int) stops.size(); ++i)
    {
        const auto& s = stops[(size_t) i];
        auto* area = areaOf (s);
        if (i == from || area == nullptr || ! (s.panel != nullptr ? s.panel->isShown() : area->isVisible()))
            continue;
        const auto r = rectOf (area);

        // How far away it is that way, and how far it is off to the side.
        int gap = 0, beside = 0;
        double offCentre = 0.0;
        const auto spanApart = [] (int a0, int a1, int b0, int b1) { return std::max (0, std::max (a0, b0) - std::min (a1, b1)); };
        switch (arrowKey)
        {
            case Qt::Key_Right: if (r.left() < here.right() - slack) continue; gap = r.left() - here.right(); break;
            case Qt::Key_Left:  if (r.right() > here.left() + slack) continue; gap = here.left() - r.right(); break;
            case Qt::Key_Down:  if (r.top() < here.bottom() - slack) continue; gap = r.top() - here.bottom(); break;
            case Qt::Key_Up:    if (r.bottom() > here.top() + slack) continue; gap = here.top() - r.bottom(); break;
            default: return;
        }
        if (arrowKey == Qt::Key_Left || arrowKey == Qt::Key_Right)
        {
            beside = spanApart (here.top(), here.bottom(), r.top(), r.bottom());
            offCentre = std::abs (r.center().y() - here.center().y());
        }
        else
        {
            beside = spanApart (here.left(), here.right(), r.left(), r.right());
            offCentre = std::abs (r.center().x() - here.center().x());
        }
        const double score = std::max (0, gap) + 3.0 * beside + 0.05 * offCentre;
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }

    if (best < 0)
    {
        statusBar()->showMessage (tr ("No panel that way"), 1500);
        return;
    }
    panelStop = best;
    cycleFrom = nullptr;
    focusStop (stops[(size_t) best]);
}

// Ctrl+W, Super+W: a panel with the keyboard closes (a popped-out one goes back to its dock,
// hidden), the mixer in the editor's place closes, and only from the editor does the file close.
void MainWindow::closeFocused()
{
    // The mixer in the editor's place, when it or nothing else has the keyboard.
    const bool panelFocused = std::any_of (panels.begin(), panels.end(), [] (DockablePanel* p) { return p->isShown() && p->containsFocus(); });
    if (mixerDocked() && mixerWin->isVisible() && (holds (mixerWin, QApplication::focusWidget()) || ! panelFocused))
    {
        mixerWin->close();
        return;
    }
    for (auto* p : panels)
        if (p->isShown() && p->containsFocus())
        {
            p->hide();
            if (p == openedByCycle)
                openedByCycle = nullptr;
            activateWindow();
            if (mixerDocked() && centralStack->currentWidget() == mixerWin)
                mixerWin->setFocus();
            else
                waveform->setFocus();
            statusBar()->showMessage (tr ("Closed %1").arg (p->dock()->windowTitle()), 1500);
            return;
        }
    // Close stays on with no file, for the panels and the mixer; then it has nothing to do.
    if (doc.isOpen() || tabs.size() > 1)
        closeFile();
}

bool MainWindow::isPanelWindow (const QWidget* w) const
{
    for (auto* p : panels)
        if (p->isPoppedOut() && p->content()->window() == w)
            return true;
    return false;
}

void MainWindow::toggleFullScreen (QWidget* w)
{
    if (w == nullptr)
        return;
    // The mixer in the editor's place comes out into a window of its own to go full screen,
    // and goes back when full screen ends.
    if (mixerDocked() && holds (mixerWin, QApplication::focusWidget()) && w == this)
    {
        placeMixer (false);
        mixerFullFromDock = true;
        mixerWin->showFullScreen();
        return;
    }
    if (w == mixerWin && mixerWin->isFullScreen() && mixerFullFromDock)
    {
        mixerFullFromDock = false;
        mixerWin->showNormal();
        placeMixer (true);
        return;
    }
    if (w->isFullScreen())
        w->showNormal();
    else
        w->showFullScreen();
}

bool MainWindow::navigationKey (QObject* o, QEvent* e)
{
    const auto type = e->type();
    // Alt tapped on its own goes to the next panel; a click or leaving the window in between
    // (Alt+Tab to another app) means it was not a tap.
    if (type == QEvent::MouseButtonPress || type == QEvent::Wheel || type == QEvent::WindowDeactivate)
        altArmed = false;
    if ((type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease) || ! o->isWidgetType())
        return false;
    auto* k = static_cast<QKeyEvent*> (e);
    auto* window = static_cast<QWidget*> (o)->window();
    const auto mods = k->modifiers() & ~Qt::KeypadModifier;
    const auto key = k->key();
    if (QApplication::activePopupWidget() != nullptr)
        return false;

    // Our other windows (the undocked mixer, the tracker, the patchbay, batch, analysis...) close
    // on Ctrl+W and Super+W and go full screen on Super+F, as the editor's panels do.
    const bool ours = window == this || isPanelWindow (window);
    if (! ours && type != QEvent::KeyRelease)
    {
        const bool closeKey = key == Qt::Key_W && (mods == Qt::ControlModifier || mods == Qt::MetaModifier);
        const bool fullKey = key == Qt::Key_F && mods == Qt::MetaModifier;
        const auto pressed = QKeySequence (k->keyCombination());
        const bool dockKey = window == mixerWin && mixerWin != nullptr
                          && (commands.action (QStringLiteral ("mixer.dock"))->shortcuts().contains (pressed)
                              || commands.action (QStringLiteral ("view.popOut"))->shortcuts().contains (pressed)
                              || commands.action (QStringLiteral ("mixer.window"))->shortcuts().contains (pressed));
        if (! closeKey && ! fullKey && ! dockKey)
            return false;
        if (type == QEvent::ShortcutOverride)
        {
            e->accept();
            return true;
        }
        if (k->isAutoRepeat())
            return true;
        QPointer<QWidget> w = window;
        QTimer::singleShot (0, this, [this, w, closeKey, fullKey, pressed]
        {
            if (w == nullptr)
                return;
            if (closeKey)
                w->close();
            else if (fullKey)
                toggleFullScreen (w);
            else if (commands.action (QStringLiteral ("mixer.window"))->shortcuts().contains (pressed))
                mixerWin->close();   // F3 again closes it
            else
            {
                if (mixerWin->isFullScreen())
                    mixerWin->showNormal();
                mixerFullFromDock = false;
                placeMixer (true);
            }
        });
        return true;
    }
    if (! ours)
        return false;

    // Alt, under the left hand: tapped on its own it goes to the next panel, held with an
    // arrow it goes to the panel that way. Alt with any other key (menus, Ctrl+Alt+arrows)
    // is left alone.
    if (key == Qt::Key_Alt)
    {
        if (type == QEvent::ShortcutOverride)
            e->accept();
        else if (type == QEvent::KeyPress && ! k->isAutoRepeat())
        {
            altArmed = true;
            altMoved = false;
        }
        else if (type == QEvent::KeyRelease && ! k->isAutoRepeat())
        {
            if (altArmed && ! altMoved)
                focusPanel (1);
            altArmed = false;
        }
        return true;
    }
    if (isArrow (key) && mods == Qt::AltModifier && type != QEvent::KeyRelease)
    {
        if (type == QEvent::ShortcutOverride)
            e->accept();
        else
        {
            altMoved = true;
            focusDirection (key);
        }
        return true;
    }
    if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress)
        altArmed = false;   // a shortcut can take the key before its KeyPress arrives

    // The Next Panel key (F6 unless rebound in Keyboard Shortcuts) goes round on its release,
    // unless an arrow was pressed while it was held.
    const auto* next = commands.action (QStringLiteral ("view.nextPanel"));
    const auto* previous = commands.action (QStringLiteral ("view.previousPanel"));
    const auto pressedKeys = QKeySequence (k->keyCombination());
    const bool nextKey = ! isArrow (key) && next->shortcuts().contains (pressedKeys);
    const bool previousKey = ! isArrow (key) && previous->shortcuts().contains (pressedKeys);
    if (nextKey || (type == QEvent::KeyRelease && panelKeyHeld && key == panelKey))
    {
        if (type == QEvent::ShortcutOverride)
            e->accept();
        else if (type == QEvent::KeyPress && ! k->isAutoRepeat())
        {
            panelKeyHeld = true;
            panelKey = key;
            panelKeyMoved = false;
        }
        else if (type == QEvent::KeyRelease && ! k->isAutoRepeat() && panelKeyHeld)
        {
            panelKeyHeld = false;
            if (! panelKeyMoved)
                focusPanel (1);
        }
        return true;
    }
    if (panelKeyHeld && isArrow (key) && type != QEvent::KeyRelease)
    {
        if (type == QEvent::ShortcutOverride)
            e->accept();
        else
        {
            panelKeyMoved = true;
            focusDirection (key);
        }
        return true;
    }
    // Previous Panel here too, so the mixer in the editor's place does not take it for its own.
    if (previousKey && type != QEvent::KeyRelease)
    {
        if (type == QEvent::ShortcutOverride)
            e->accept();
        else if (! k->isAutoRepeat())
            focusPanel (-1);
        return true;
    }

    // The mixer in the editor's place keeps its own keys, as it did in its own window: only
    // the window's commands for closing, docking, full screen, the panels and the menus get past it.
    if (type == QEvent::ShortcutOverride && mixerDocked() && holds (mixerWin, static_cast<QWidget*> (o)))
    {
        const auto pressed = QKeySequence (k->keyCombination());
        static const char* const passing[] = { "file.close", "view.popOut", "mixer.window", "mixer.dock", "view.fullScreen", "view.focusWaveform",
                                               "help.palette", "help.menuBar", "mixer.mixdown" };
        for (const auto* id : passing)
            if (auto* a = commands.action (QString::fromLatin1 (id)); a != nullptr && a->shortcuts().contains (pressed))
                return false;
        if (mods == Qt::AltModifier || (mods & Qt::MetaModifier) != 0)
            return false;   // the menus' Alt keys and the desktop's Super keys
        e->accept();
        return true;
    }
    return false;
}

void MainWindow::dragEnterEvent (QDragEnterEvent* e)
{
    if (! localFiles (e->mimeData()).isEmpty())
        e->acceptProposedAction();
}

void MainWindow::dropEvent (QDropEvent* e)
{
    const auto paths = localFiles (e->mimeData());
    if (paths.isEmpty())
        return;
    e->acceptProposedAction();
    // After the drop finishes, so the file manager is not kept waiting.
    QTimer::singleShot (0, this, [this, paths] { openDropped (paths); });
}

void MainWindow::openDropped (const QStringList& paths)
{
    showEditorPage();
    activateWindow();
    // With an album in front, songs join it, as from the Find panel.
    if (album() != nullptr)
    {
        QStringList songs;
        for (const auto& p : paths)
            if (albumfile::isAlbumFile (p))
                openFile (p);
            else
                songs << p;
        if (! songs.isEmpty())
            addSongFiles (songs);
        return;
    }
    for (const auto& p : paths)
        openFile (p);
}
