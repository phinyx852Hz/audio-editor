// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"
#include "../core/Metadata.h"
#include "../engine/TagIO.h"

#include <QDialog>

#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;
class QWidget;

// The metadata editor, modeled on Audition's Metadata panel: a Common tab whose
// values every schema follows, a tab per schema (XMP with all of Adobe's
// schemas, BWF, Cart, iXML, ID3, RIFF, Vorbis, MP4, APE), cover art, the raw
// chunks and frames of the file, and which schemas are written. A schema field
// left empty follows the Common value (shown greyed); typing in it gives that
// schema its own value, shown as a conflict until synced.
class MetadataEditor final : public QDialog
{
    Q_OBJECT

public:
    MetadataEditor (const Tags&, const QString& path, double sampleRate, QWidget* parent);
    Tags tags() const { return current; }

    // Which schemas Save writes for a file type (remembered per extension).
    static tagio::Options writeOptionsFor (const QString& extension);
    static void saveWriteOptions (const QString& extension, const tagio::Options&);

    // Saved templates: name -> "key=value" lines with {variables}.
    static QStringList templateNames();
    static QString templateText (const QString& name);
    static void saveTemplate (const QString& name, const QString& text);
    static void removeTemplate (const QString& name);
    // The {variables} a template can use, with what they mean.
    static QString variablesHelp();

private:
    void buildCommon (QTabWidget*);
    void buildPictures (QTabWidget*);
    void buildSchema (QTabWidget*, meta::Schema, const QString& title, const QString& note);
    void buildXmp (QTabWidget*);
    void buildRaw (QTabWidget*);
    void buildWrite (QTabWidget*);
    QWidget* fieldEditor (const meta::Field&);
    void refreshAll();
    void refreshPictures();
    void refreshConflicts();
    void refreshXmp();
    void importRows();
    void exportRows();
    void applyTemplate();
    void saveAsTemplate();

    Tags current;
    QString path, extension;
    double sampleRate;
    bool updating = false;

    struct Bound
    {
        const meta::Field* field = nullptr;   // null: a common key
        std::string key;
        QWidget* editor = nullptr;
        QLabel* label = nullptr;
    };
    std::vector<Bound> bound;
    std::vector<std::function<void()>> refreshers;

    QListWidget* pictureList = nullptr;
    QLabel* picturePreview = nullptr;
    QTreeWidget* xmpTree = nullptr;
    QLineEdit* xmpSearch = nullptr;
    QListWidget* conflictList = nullptr;
    QListWidget* keptList = nullptr;
    tagio::Options writeOptions;
    std::string pictureSignature;
    bool xmpDirty = true;
};
