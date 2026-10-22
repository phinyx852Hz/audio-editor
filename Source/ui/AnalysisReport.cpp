// SPDX-License-Identifier: AGPL-3.0-only
#include "AnalysisReport.h"
#include "Settings.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

AnalysisReport::AnalysisReport (const QString& text, const QString& audioPath, QWidget* parent)
    : QWidget (parent, Qt::Window), audio (audioPath)
{
    setAttribute (Qt::WA_DeleteOnClose);
    setObjectName (QStringLiteral ("AnalysisReport"));
    setWindowTitle (tr ("Analysis Report: %1").arg (QFileInfo (audioPath).fileName()));
    Theme::makeTranslucent (this);

    view = new QPlainTextEdit (this);
    view->setReadOnly (true);
    view->setLineWrapMode (QPlainTextEdit::NoWrap);
    view->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
    view->setPlainText (text);
    view->setAccessibleName (tr ("Report"));

    auto button = [this] (const QString& label, const QString& tip)
    {
        auto* b = new QPushButton (label, this);
        b->setToolTip (tip);
        b->setFocusPolicy (Qt::NoFocus);
        return b;
    };
    auto* saveButton = button (tr ("Save"), tr ("Save in the report folder (Ctrl+S)"));
    connect (saveButton, &QPushButton::clicked, this, [this] { save(); });
    auto* saveAsButton = button (tr ("Save As..."), tr ("Save anywhere (Ctrl+Shift+S)"));
    connect (saveAsButton, &QPushButton::clicked, this, [this] { saveAs(); });
    auto* copyButton = button (tr ("Copy"), tr ("Copy the whole report"));
    connect (copyButton, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText (view->toPlainText()); });
    auto* folderButton = button (tr ("Folder..."), tr ("Where Save puts reports"));
    connect (folderButton, &QPushButton::clicked, this, [this]
    {
        const auto folder = QFileDialog::getExistingDirectory (this, tr ("Report Folder"), defaultFolder());
        if (! folder.isEmpty())
            setDefaultFolder (folder);
        updateNote();
    });
    auto* closeButton = button (tr ("Close"), tr ("Close the report (Ctrl+W); an unsaved one asks first"));
    connect (closeButton, &QPushButton::clicked, this, &QWidget::close);

    note = new QLabel (this);
    note->setForegroundRole (QPalette::PlaceholderText);
    note->setTextInteractionFlags (Qt::TextSelectableByMouse);

    auto* row = new QHBoxLayout;
    row->addWidget (note, 1);
    row->addWidget (folderButton);
    row->addWidget (copyButton);
    row->addWidget (saveAsButton);
    row->addWidget (saveButton);
    row->addWidget (closeButton);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (10, 10, 10, 10);
    layout->addWidget (view, 1);
    layout->addLayout (row);

    updateNote();
    const auto width = view->fontMetrics().horizontalAdvance (QString (86, QLatin1Char ('0')));
    resize (width + 60, 820);
}

QString AnalysisReport::defaultFolder()
{
    const auto fallback = QStandardPaths::writableLocation (QStandardPaths::DocumentsLocation) + QStringLiteral ("/" APP_DISPLAY_NAME " Analysis");
    return appSettings().value (QStringLiteral ("analysis/reportFolder"), fallback).toString();
}

void AnalysisReport::setDefaultFolder (const QString& folder)
{
    appSettings().setValue (QStringLiteral ("analysis/reportFolder"), folder);
}

void AnalysisReport::updateNote()
{
    note->setText (! savedTo.isEmpty() ? tr ("Saved to %1").arg (QDir::toNativeSeparators (savedTo))
                                       : tr ("Not saved: closing without saving discards it. Save puts it in %1")
                                             .arg (QDir::toNativeSeparators (defaultFolder())));
}

QString AnalysisReport::suggestedName() const
{
    const auto base = QFileInfo (audio).completeBaseName();
    const QDir folder (defaultFolder());
    auto name = tr ("%1 - analysis.txt").arg (base);
    for (int n = 2; folder.exists (name); ++n)
        name = tr ("%1 - analysis (%2).txt").arg (base).arg (n);
    return folder.filePath (name);
}

bool AnalysisReport::write (const QString& path)
{
    QDir().mkpath (QFileInfo (path).absolutePath());
    QSaveFile file (path);
    if (! file.open (QIODevice::WriteOnly | QIODevice::Text) || file.write (view->toPlainText().toUtf8()) < 0 || ! file.commit())
    {
        QMessageBox::warning (this, tr ("Save Report"), tr ("%1 could not be written: %2").arg (path, file.errorString()));
        return false;
    }
    savedTo = path;
    updateNote();
    return true;
}

bool AnalysisReport::save()
{
    return write (suggestedName());
}

bool AnalysisReport::saveAs()
{
    const auto path = QFileDialog::getSaveFileName (this, tr ("Save Report"), suggestedName(), tr ("Text files (*.txt)"));
    return ! path.isEmpty() && write (path);
}

void AnalysisReport::keyPressEvent (QKeyEvent* e)
{
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (e->key() == Qt::Key_W && (mods == Qt::ControlModifier || mods == Qt::MetaModifier))
        close();
    else if (e->key() == Qt::Key_Escape && mods == Qt::NoModifier)
        close();
    else if (e->key() == Qt::Key_S && mods == Qt::ControlModifier)
        save();
    else if (e->key() == Qt::Key_S && mods == (Qt::ControlModifier | Qt::ShiftModifier))
        saveAs();
    else
        QWidget::keyPressEvent (e);
}

// Super+W on Hyprland and Omarchy closes the window through the desktop, which lands here too.
void AnalysisReport::closeEvent (QCloseEvent* e)
{
    if (savedTo.isEmpty())
    {
        QMessageBox box (QMessageBox::Question, tr ("Save Report?"),
                         tr ("Save the analysis of %1 before closing? If not, it is gone.").arg (QFileInfo (audio).fileName()),
                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.setInformativeText (tr ("Save puts it in %1").arg (QDir::toNativeSeparators (defaultFolder())));
        box.setDefaultButton (QMessageBox::Save);
        const auto answer = box.exec();
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && ! save()))
        {
            e->ignore();
            return;
        }
    }
    QWidget::closeEvent (e);
}
