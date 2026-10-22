// SPDX-License-Identifier: AGPL-3.0-only
// Projects (.phi, docs/project.md): what the mixer saves of its mix, with the
// patchbay and the view added, so a production comes back as it was left. The file
// is the mixer's (MixerWindow::writeMix and loadMix); this is the rest of it, and
// last.phi in the config folder, written on quit and opened again at the next start.
#include "MainWindow.h"
#include "MixerWindow.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStackedWidget>

namespace
{
    QJsonObject readProject (const QString& file)
    {
        QFile in (file);
        if (! in.open (QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson (in.readAll()).object();
    }

    bool showsMixer (const QJsonObject& project)
    {
        return project.value ("view").toObject().value ("mixer").toBool();
    }
}

QJsonObject MainWindow::projectState() const
{
    return { { "patchbay", patchBayJson() }, { "view", QJsonObject { { "mixer", mixerInView() } } } };
}

void MainWindow::setProjectState (const QJsonObject& project)
{
    // A mix from before projects has no patchbay: the one there is stays.
    if (project.contains (QStringLiteral ("patchbay")))
    {
        setPatchBayJson (project.value ("patchbay").toObject());
        projectPatchBay = true;
    }
}

// The view hook: true while the Mixer is what is in front, rather than the editor.
bool MainWindow::mixerInView() const
{
    // In the editor's place it is in view only while it is the page shown.
    return mixerWin != nullptr && mixerWin->isVisible() && (! mixerDocked() || centralStack->currentWidget() == (QWidget*) mixerWin);
}

void MainWindow::openProject (const QString& file)
{
    auto* w = mixerWindow();
    if (w->openMixFile (file))
        showMixer();
}

void MainWindow::restoreProject()
{
    const auto file = MixerWindow::autosavePath();
    const auto project = readProject (file);
    if (mixerWin == nullptr && ! project.isEmpty())
        mixerWindow();   // opens last.phi, patchbay and all
    // The patchbay of a time before projects, or of a project that has none, from settings.ini.
    if (! projectPatchBay)
        restorePatchBay();
    if (mixerWin != nullptr && showsMixer (project) && ! mixerInView())
        showMixer();
}
