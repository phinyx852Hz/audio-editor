// SPDX-License-Identifier: AGPL-3.0-only
// Projects (.phi, docs/project.md): what the mixer saves of its mix, with the
// patchbay and the view added, so a production comes back as it was left. The file
// is the mixer's (MixerWindow::writeMix and loadMix); this is the rest of it, and
// last.phi in the config folder, written while PHI runs and offered back after a crash.
#include "MainWindow.h"
#include "MixerWindow.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
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
    // A clean quit removes last.phi, so PHI starts on a blank project with the default
    // patchbay (owner, 2026-10-30). One left behind is a crash's, offered back once.
    const auto file = MixerWindow::autosavePath();
    const auto project = readProject (file);
    if (project.isEmpty())
        return;
    const auto name = MixerWindow::autosaveName();
    if (QMessageBox::question (this, tr ("Recover Project"),
                               tr ("PHI did not close properly last time. Recover %1 as it was then?")
                                   .arg (name.isEmpty() ? tr ("the untitled project") : name),
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
    {
        MixerWindow::removeAutosave();
        return;
    }
    if (mixerWin == nullptr)
        mixerWindow();   // opens last.phi, patchbay and all
    // The patchbay of a time before projects, or of a project that has none, from settings.ini.
    if (! projectPatchBay)
        restorePatchBay();
    if (mixerWin != nullptr && showsMixer (project) && ! mixerInView())
        showMixer();
}
