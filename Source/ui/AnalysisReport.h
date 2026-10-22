// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;

// A text report from the Analysis Meters window, in its own window. Nothing is
// written until you save: Save (Ctrl+S) puts it in the report folder (Folder...
// changes it, remembered; ~/Documents/PHINYX Analysis at first), Save As picks
// anywhere. Closing an unsaved report (Ctrl+W, Super+W, Esc or the window's close
// button) asks whether to save it; Discard lets it go.
class AnalysisReport final : public QWidget
{
    Q_OBJECT

public:
    AnalysisReport (const QString& text, const QString& audioPath, QWidget* parent);

    static QString defaultFolder();
    static void setDefaultFolder (const QString&);

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void closeEvent (class QCloseEvent*) override;

private:
    bool save();                        // to the report folder
    bool saveAs();
    bool write (const QString& path);
    QString suggestedName() const;      // "Song - analysis.txt", numbered when taken
    void updateNote();

    QString audio;
    QPlainTextEdit* view;
    QLabel* note;
    QString savedTo;
};
