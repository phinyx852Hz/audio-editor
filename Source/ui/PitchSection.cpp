// SPDX-License-Identifier: AGPL-3.0-only
// Repair > Correct Pitch: moves a selected note to the right pitch.
#include "MainWindow.h"
#include "../core/PitchFix.h"
#include "../core/SpectrumView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>

void MainWindow::correctPitch()
{
    const auto d = data();
    if (d == nullptr || ! requireSelection() || refuseAlbumEdit())
        return;

    // Works on the selection and a little around it, so the tracker sees the note's edges.
    const auto start = selStart, end = selEnd;
    const auto context = (Frame) (0.1 * d->sampleRate());
    const auto from = std::max<Frame> (0, start - context), to = std::min (d->length(), end + context);
    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto piece = d->slice (from, to);
    const std::shared_ptr<const pitch::Track> track = pitch::track (*piece);
    const auto centre = pitchfix::centreHz (*track, start - from, end - from);
    QApplication::restoreOverrideCursor();
    if (centre <= 0.0)
    {
        QMessageBox::information (this, tr ("Correct Pitch"),
                                  tr ("No steady pitch was found here. Select one sung or played note (the pitch line, Shift+P, shows where they are) and try again."));
        return;
    }

    QSettings settings;
    settings.beginGroup (QStringLiteral ("repair/pitch"));

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Correct Pitch"));
    auto* form = new QFormLayout;

    const auto nearest = pitchfix::nearestNoteHz (centre);
    const auto off = (int) std::lround (pitchfix::cents (nearest, centre));
    const auto nearestName = QString::fromStdString (spectrum::noteName (nearest));
    auto* found = new QLabel (tr ("%1 Hz, %2").arg (centre, 0, 'f', 1)
                                  .arg (off == 0 ? tr ("right on %1").arg (nearestName)
                                                 : off < 0 ? tr ("%1 cents flat of %2").arg (-off).arg (nearestName)
                                                           : tr ("%1 cents sharp of %2").arg (off).arg (nearestName)));
    found->setTextInteractionFlags (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    form->addRow (tr ("This note:"), found);

    // Notes from C1 to C8; the nearest is chosen.
    auto* target = new QComboBox;
    const auto nearestMidi = (int) std::lround (69.0 + 12.0 * std::log2 (nearest / 440.0));
    for (int midi = 24; midi <= 108; ++midi)
    {
        const auto hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        target->addItem (QStringLiteral ("%1  (%2 Hz)").arg (QString::fromStdString (spectrum::noteName (hz))).arg (hz, 0, 'f', 1), hz);
    }
    target->setCurrentIndex (std::clamp (nearestMidi - 24, 0, target->count() - 1));
    target->setToolTip (tr ("The note to move it to; the nearest is chosen for you"));
    form->addRow (tr ("&Move to:"), target);

    auto* amount = new QSpinBox;
    amount->setRange (0, 100);
    amount->setSuffix (tr (" %"));
    amount->setValue (settings.value (QStringLiteral ("amount"), 100).toInt());
    amount->setToolTip (tr ("How far to move it: 100 % lands exactly on the note, less keeps some of the original"));
    form->addRow (tr ("&Amount:"), amount);

    auto* flatten = new QSpinBox;
    flatten->setRange (0, 100);
    flatten->setSuffix (tr (" %"));
    flatten->setValue (settings.value (QStringLiteral ("flatten"), 0).toInt());
    flatten->setToolTip (tr ("Takes out wavering and vibrato: 0 % keeps the performance, 100 % holds the note dead steady"));
    form->addRow (tr ("&Flatten drift and vibrato:"), flatten);

    auto* formants = new QCheckBox (tr ("&Keep the voice's character (formants)"));
    formants->setChecked (settings.value (QStringLiteral ("formants"), true).toBool());
    formants->setToolTip (tr ("Avoids the chipmunk or giant sound when a note moves a long way"));
    form->addRow (QString(), formants);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout (&dialog);
    layout->addLayout (form);
    layout->addWidget (buttons);
    target->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue (QStringLiteral ("amount"), amount->value());
    settings.setValue (QStringLiteral ("flatten"), flatten->value());
    settings.setValue (QStringLiteral ("formants"), formants->isChecked());

    pitchfix::Settings fix;
    fix.targetHz = target->currentData().toDouble();
    fix.amount = amount->value() / 100.0;
    fix.flatten = flatten->value() / 100.0;
    fix.keepFormants = formants->isChecked();

    auto error = std::make_shared<std::string>();
    auto result = runRepair (tr ("Correct Pitch"), [piece, track, fix, error, s = start - from, e = end - from] (repair::Progress p)
    {
        return pitchfix::correct (*piece, s, e, *track, fix, *error, p.done);
    });
    if (result == nullptr)
    {
        if (! error->empty())
            QMessageBox::information (this, tr ("Correct Pitch"), QString::fromStdString (*error));
        return;
    }
    const auto note = target->currentText().section (QLatin1Char (' '), 0, 0);
    commitRepair (tr ("Correct Pitch to %1").arg (note), d->replaced (start, end, result.get()), start, end);
    statusBar()->showMessage (tr ("Moved the note to %1").arg (note), 6000);
}
