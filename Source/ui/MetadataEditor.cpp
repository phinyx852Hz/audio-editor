// SPDX-License-Identifier: AGPL-3.0-only
#include "MetadataEditor.h"
#include "Settings.h"

#include "../core/RiffChunks.h"
#include "../core/Xmp.h"
#include "../engine/AudioEngine.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <set>

namespace
{
    QString qs (const std::string& s) { return QString::fromStdString (s); }

    std::string valueOf (const Tags& tags, const std::string& key)
    {
        const auto it = tags.find (key);
        return it == tags.end() ? std::string() : it->second;
    }

    QString editorText (QWidget* w)
    {
        if (auto* line = qobject_cast<QLineEdit*> (w))
            return line->text();
        if (auto* text = qobject_cast<QPlainTextEdit*> (w))
            return text->toPlainText();
        return {};
    }

    void setEditor (QWidget* w, const QString& text, const QString& placeholder)
    {
        const QSignalBlocker block (w);
        if (auto* line = qobject_cast<QLineEdit*> (w))
        {
            if (line->text() != text)
                line->setText (text);
            line->setPlaceholderText (placeholder);
        }
        else if (auto* edit = qobject_cast<QPlainTextEdit*> (w))
        {
            if (edit->toPlainText() != text)
                edit->setPlainText (text);
            edit->setPlaceholderText (placeholder);
        }
    }

    QString commonLabel (const std::string& key)
    {
        for (const auto& [k, label] : meta::commonKeys())
            if (k == key)
                return qs (label);
        return qs (key);
    }

    QString settingsKey (const QString& extension, const char* schema)
    {
        return QStringLiteral ("metadata/write/%1/%2").arg (extension.toLower(), QString::fromLatin1 (schema));
    }

    QWidget* scrolling (QWidget* inner)
    {
        auto* area = new QScrollArea;
        area->setWidget (inner);
        area->setWidgetResizable (true);
        area->setFrameShape (QFrame::NoFrame);
        return area;
    }
}

//==============================================================================
tagio::Options MetadataEditor::writeOptionsFor (const QString& extension)
{
    auto s = appSettings();
    tagio::Options o;
    const auto read = [&] (const char* name, bool def) { return s.value (settingsKey (extension, name), def).toBool(); };
    o.riff = read ("riff", true);
    o.bext = read ("bext", true);
    o.cart = read ("cart", true);
    o.ixml = read ("ixml", true);
    o.id3 = read ("id3", extension.toLower() != QStringLiteral ("wav"));   // ID3 in WAV only when asked
    o.id3v1 = read ("id3v1", true);
    o.vorbis = read ("vorbis", true);
    o.mp4 = read ("mp4", true);
    o.ape = read ("ape", true);
    o.xmp = read ("xmp", true);
    o.chapters = read ("chapters", true);
    o.id3Version = s.value (settingsKey (extension, "id3Version"), 4).toInt() == 3 ? 3 : 4;
    o.software = APP_DISPLAY_NAME;
    return o;
}

void MetadataEditor::saveWriteOptions (const QString& extension, const tagio::Options& o)
{
    auto s = appSettings();
    const std::pair<const char*, bool> flags[] { { "riff", o.riff }, { "bext", o.bext }, { "cart", o.cart }, { "ixml", o.ixml }, { "id3", o.id3 },
                                                 { "id3v1", o.id3v1 }, { "vorbis", o.vorbis }, { "mp4", o.mp4 }, { "ape", o.ape },
                                                 { "xmp", o.xmp }, { "chapters", o.chapters } };
    for (const auto& [name, on] : flags)
        s.setValue (settingsKey (extension, name), on);
    s.setValue (settingsKey (extension, "id3Version"), o.id3Version);
}

QStringList MetadataEditor::templateNames()
{
    auto s = appSettings();
    s.beginGroup (QStringLiteral ("metadata/templates"));
    auto names = s.childKeys();
    for (auto& n : names)
        n = QUrl::fromPercentEncoding (n.toUtf8());
    names.sort (Qt::CaseInsensitive);
    return names;
}

QString MetadataEditor::templateText (const QString& name)
{
    return appSettings().value (QStringLiteral ("metadata/templates/") + QString::fromUtf8 (QUrl::toPercentEncoding (name))).toString();
}

void MetadataEditor::saveTemplate (const QString& name, const QString& text)
{
    appSettings().setValue (QStringLiteral ("metadata/templates/") + QString::fromUtf8 (QUrl::toPercentEncoding (name)), text);
}

void MetadataEditor::removeTemplate (const QString& name)
{
    appSettings().remove (QStringLiteral ("metadata/templates/") + QString::fromUtf8 (QUrl::toPercentEncoding (name)));
}

QString MetadataEditor::variablesHelp()
{
    return tr ("Variables: {title}, {artist}, {album}, {track} and every other common field; {filename} (without the extension), "
               "{today} (yyyy-mm-dd), {year}. When exporting also {part} (region or marker name), {index}, {output}, {format}, "
               "{ext}, {rate} (kHz), {bits}, {bitrate}, {lufs}, {truepeak} and {lra} (measured).");
}

//==============================================================================
MetadataEditor::MetadataEditor (const Tags& tagsIn, const QString& filePath, double rate, QWidget* parent)
    : QDialog (parent), current (tagsIn), path (filePath), extension (QFileInfo (filePath).suffix().toLower()), sampleRate (rate)
{
    if (extension.isEmpty())
        extension = QStringLiteral ("wav");
    writeOptions = writeOptionsFor (extension);
    setWindowTitle (tr ("Metadata"));
    resize (760, 640);

    auto* tabs = new QTabWidget (this);
    tabs->setUsesScrollButtons (true);
    connect (tabs, &QTabWidget::currentChanged, this, [this] { if (xmpDirty) refreshXmp(); });
    buildCommon (tabs);
    buildPictures (tabs);
    buildXmp (tabs);
    buildSchema (tabs, meta::Schema::bext, tr ("BWF"), tr ("Broadcast Wave (bext chunk, EBU Tech 3285 v2). WAV only. The description follows the Common comment."));
    buildSchema (tabs, meta::Schema::id3, tr ("ID3"), tr ("ID3v2 frames, in MP3, AIFF and (when chosen on the Write tab) WAV. ID3v1 is written from the same values."));
    buildSchema (tabs, meta::Schema::riff, tr ("RIFF"), tr ("RIFF INFO list and Display Title (WAV)."));
    buildSchema (tabs, meta::Schema::cart, tr ("Cart"), tr ("AES46 cart chunk for radio playout systems (WAV). Timers count samples from the start."));
    buildSchema (tabs, meta::Schema::ixml, tr ("iXML"), tr ("iXML production sound metadata (WAV). Other elements of the file are kept as they are."));
    buildSchema (tabs, meta::Schema::vorbis, tr ("Vorbis"), tr ("Vorbis comments, in FLAC, Ogg Vorbis, Opus and Speex."));
    buildSchema (tabs, meta::Schema::mp4, tr ("MP4"), tr ("iTunes atoms, in M4A (AAC and ALAC)."));
    buildSchema (tabs, meta::Schema::ape, tr ("APE"), tr ("APEv2 tags, in WavPack, Monkey's Audio and Musepack."));
    buildRaw (tabs);
    buildWrite (tabs);

    // Bottom row: templates, spreadsheets.
    auto* templates = new QToolButton (this);
    templates->setText (tr ("&Templates"));
    templates->setPopupMode (QToolButton::InstantPopup);
    auto* templateMenu = new QMenu (templates);
    templates->setMenu (templateMenu);
    connect (templateMenu, &QMenu::aboutToShow, this, [this, templateMenu]
    {
        templateMenu->clear();
        templateMenu->addAction (tr ("&Apply a Template..."), this, &MetadataEditor::applyTemplate);
        templateMenu->addAction (tr ("&Save as Template..."), this, &MetadataEditor::saveAsTemplate);
        auto* remove = templateMenu->addMenu (tr ("&Delete a Template"));
        for (const auto& name : templateNames())
            remove->addAction (name, this, [name] { removeTemplate (name); });
        remove->setEnabled (! remove->isEmpty());
    });
    auto* importButton = new QPushButton (tr ("&Import..."), this);
    importButton->setToolTip (tr ("Read the tags from a CSV or JSON file (a row whose file is this one, or the first row)"));
    auto* exportButton = new QPushButton (tr ("E&xport..."), this);
    exportButton->setToolTip (tr ("Write the tags to a CSV or JSON file, to edit in a spreadsheet"));
    connect (importButton, &QPushButton::clicked, this, &MetadataEditor::importRows);
    connect (exportButton, &QPushButton::clicked, this, &MetadataEditor::exportRows);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect (buttons, &QDialogButtonBox::accepted, this, [this]
    {
        saveWriteOptions (extension, writeOptions);
        accept();
    });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget (templates);
    bottom->addWidget (importButton);
    bottom->addWidget (exportButton);
    bottom->addStretch (1);
    bottom->addWidget (buttons);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (tabs, 1);
    layout->addLayout (bottom);
    refreshAll();
    if (! bound.empty())
        bound.front().editor->setFocus();
}

QWidget* MetadataEditor::fieldEditor (const meta::Field& f)
{
    QWidget* editor;
    if (f.kind == meta::Kind::lines)
    {
        auto* edit = new QPlainTextEdit;
        edit->setTabChangesFocus (true);
        edit->setFixedHeight (edit->fontMetrics().lineSpacing() * 3 + 12);
        connect (edit, &QPlainTextEdit::textChanged, this, [this, edit, key = f.key]
        {
            if (updating)
                return;
            const auto text = edit->toPlainText().toStdString();
            if (text.empty())
                current.erase (key);
            else
                current[key] = text;
            refreshAll();
        });
        editor = edit;
    }
    else
    {
        auto* line = new QLineEdit;
        if (f.maxLength > 0)
            line->setMaxLength (f.maxLength);
        connect (line, &QLineEdit::textEdited, this, [this, key = f.key] (const QString& text)
        {
            if (text.isEmpty())
                current.erase (key);
            else
                current[key] = text.toStdString();
            refreshAll();
        });
        editor = line;
    }
    if (! f.common.empty())
        editor->setToolTip (tr ("Empty: follows the Common %1. Type here to give %2 its own value.")
                                .arg (commonLabel (f.common), QString::fromLatin1 (meta::schemaName (f.schema))));
    return editor;
}

void MetadataEditor::buildCommon (QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* form = new QFormLayout (page);

    auto* sync = new QPushButton (tr ("&Sync All Schemas"));
    sync->setToolTip (tr ("Every schema takes the Common values again: values that differ (marked with \xE2\x89\xA0) are dropped"));
    connect (sync, &QPushButton::clicked, this, [this]
    {
        meta::sync (current);
        refreshAll();
    });
    auto* intro = new QHBoxLayout;
    intro->addWidget (new QLabel (tr ("Typed once here, these are written to every schema the file type holds.")), 1);
    intro->addWidget (sync);
    form->addRow (intro);

    for (const auto& [key, labelText] : meta::commonKeys())
    {
        QWidget* editor;
        const bool lines = key == tag::comment || key == tag::lyrics;
        if (lines)
        {
            auto* edit = new QPlainTextEdit;
            edit->setTabChangesFocus (true);
            edit->setFixedHeight (edit->fontMetrics().lineSpacing() * 3 + 12);
            connect (edit, &QPlainTextEdit::textChanged, this, [this, edit, k = key]
            {
                if (updating)
                    return;
                const auto text = edit->toPlainText().toStdString();
                if (text.empty())
                    current.erase (k);
                else
                    current[k] = text;
                refreshAll();
            });
            editor = edit;
        }
        else
        {
            auto* line = new QLineEdit;
            if (key == tag::isrc)
                line->setMaxLength (12), line->setPlaceholderText (tr ("CC-XXX-YY-NNNNN without dashes"));
            if (key == tag::date)
                line->setPlaceholderText (tr ("2026 or 2026-10-18"));
            connect (line, &QLineEdit::textEdited, this, [this, k = key] (const QString& text)
            {
                if (text.trimmed().isEmpty())
                    current.erase (k);
                else
                    current[k] = text.toStdString();
                refreshAll();
            });
            editor = line;
        }
        auto* label = new QLabel (qs (labelText) + QLatin1Char (':'));
        label->setBuddy (editor);
        form->addRow (label, editor);
        bound.push_back ({ nullptr, key, editor, label });
    }
    tabs->addTab (scrolling (page), tr ("&Common"));
}

void MetadataEditor::buildSchema (QTabWidget* tabs, meta::Schema schema, const QString& title, const QString& note)
{
    auto* page = new QWidget;
    auto* form = new QFormLayout (page);
    auto* about = new QLabel (note);
    about->setWordWrap (true);
    form->addRow (about);

    std::string group;
    for (const auto* f : meta::fieldsOf (schema))
    {
        if (f->group != group && schema == meta::Schema::xmp)
            form->addRow (new QLabel (QStringLiteral ("<b>%1</b>").arg (qs (f->group))));
        group = f->group;
        auto* editor = fieldEditor (*f);
        auto* label = new QLabel (QStringLiteral ("%1 <span style='color:gray'>%2</span>:").arg (qs (f->label).toHtmlEscaped(), qs (f->id).toHtmlEscaped()));
        label->setBuddy (editor);
        form->addRow (label, editor);
        bound.push_back ({ f, f->key, editor, label });
    }

    if (schema == meta::Schema::bext)
    {
        auto* fill = new QPushButton (tr ("&Fill In Now"));
        fill->setToolTip (tr ("Originator, a unique reference, and today's date and time"));
        connect (fill, &QPushButton::clicked, this, [this]
        {
            const auto now = QDateTime::currentDateTime();
            if (valueOf (current, tag::bwfOriginator).empty())
                current[tag::bwfOriginator] = APP_DISPLAY_NAME;
            if (valueOf (current, tag::bwfOriginatorRef).empty())
                current[tag::bwfOriginatorRef] = (QStringLiteral ("PHX") + now.toString (QStringLiteral ("yyyyMMddHHmmss"))).toStdString();
            current[tag::bwfDate] = now.toString (QStringLiteral ("yyyy-MM-dd")).toStdString();
            current[tag::bwfTime] = now.toString (QStringLiteral ("HH:mm:ss")).toStdString();
            refreshAll();
        });
        auto* seconds = new QLabel;
        refreshers.push_back ([this, seconds]
        {
            const auto samples = std::atoll (valueOf (current, tag::bwfTimeReference).c_str());
            const auto total = sampleRate > 0.0 ? (double) samples / sampleRate : 0.0;
            seconds->setText (tr ("Time reference = %1:%2:%3 since midnight")
                                  .arg ((int) (total / 3600.0), 2, 10, QLatin1Char ('0'))
                                  .arg ((int) std::fmod (total / 60.0, 60.0), 2, 10, QLatin1Char ('0'))
                                  .arg (std::fmod (total, 60.0), 6, 'f', 3, QLatin1Char ('0')));
        });
        form->addRow (seconds, fill);
        form->addRow (new QLabel (tr ("The loudness fields are filled in by Export when the WAV output's \"Fill in BWF loudness\" is on.")));
    }

    // Fields of this schema PHINYX has no name for (kept from the file), and new ones.
    auto* others = new QWidget;
    auto* othersForm = new QFormLayout (others);
    othersForm->setContentsMargins (0, 0, 0, 0);
    form->addRow (others);
    auto* add = new QPushButton (tr ("&Add Field..."));
    form->addRow (QString(), add);

    const auto prefix = schema == meta::Schema::bext ? std::string ("bwf.") : std::string (meta::schemaPrefix (schema)) + ":";
    auto rebuildOthers = [this, othersForm, prefix]
    {
        while (othersForm->rowCount() > 0)
            othersForm->removeRow (0);
        for (auto it = current.lower_bound (prefix); it != current.end() && it->first.compare (0, prefix.size(), prefix) == 0; ++it)
        {
            if (meta::find (it->first) != nullptr)
                continue;
            auto* line = new QLineEdit (qs (it->second));
            connect (line, &QLineEdit::editingFinished, this, [this, line, key = it->first]
            {
                if (line->text().isEmpty())
                    current.erase (key);
                else
                    current[key] = line->text().toStdString();
            });
            othersForm->addRow (qs (it->first.substr (prefix.size())) + QLatin1Char (':'), line);
        }
    };
    refreshers.push_back ([this, rebuildOthers, othersForm, prefix]
    {
        // Rebuilt only when the set of other keys changed, so typing is not interrupted.
        int count = 0;
        for (auto it = current.lower_bound (prefix); it != current.end() && it->first.compare (0, prefix.size(), prefix) == 0; ++it)
            count += meta::find (it->first) == nullptr ? 1 : 0;
        if (count != othersForm->rowCount())
            rebuildOthers();
    });
    connect (add, &QPushButton::clicked, this, [this, schema, prefix, rebuildOthers]
    {
        const auto hint = schema == meta::Schema::id3 ? tr ("A frame ID such as TMOO, or TXXX:NAME for your own field")
                          : schema == meta::Schema::ixml ? tr ("An element path such as USER/CLIENT")
                          : schema == meta::Schema::mp4 ? tr ("An atom such as \xC2\xA9" "st3, or ----:com.apple.iTunes:NAME")
                                                        : tr ("The field's name");
        bool ok = false;
        const auto id = QInputDialog::getText (this, tr ("Add Field"), hint, QLineEdit::Normal, {}, &ok).trimmed();
        if (! ok || id.isEmpty())
            return;
        const auto value = QInputDialog::getText (this, tr ("Add Field"), tr ("Value of %1:").arg (id), QLineEdit::Normal, {}, &ok);
        if (! ok || value.isEmpty())
            return;
        current[prefix + id.toStdString()] = value.toStdString();
        rebuildOthers();
        refreshAll();
    });

    tabs->addTab (scrolling (page), title);
}

void MetadataEditor::buildXmp (QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout (page);
    auto* top = new QHBoxLayout;
    xmpSearch = new QLineEdit;
    xmpSearch->setPlaceholderText (tr ("Search properties and values"));
    xmpSearch->setClearButtonEnabled (true);
    auto* display = new QToolButton;
    display->setText (tr ("&Schemas"));
    display->setToolTip (tr ("Choose which schemas are shown"));
    display->setPopupMode (QToolButton::InstantPopup);
    auto* displayMenu = new QMenu (display);
    display->setMenu (displayMenu);
    auto* add = new QPushButton (tr ("Add &Property..."));
    add->setToolTip (tr ("A property in any namespace, your own included"));
    top->addWidget (xmpSearch, 1);
    top->addWidget (display);
    top->addWidget (add);
    layout->addLayout (top);

    xmpTree = new QTreeWidget;
    xmpTree->setColumnCount (2);
    xmpTree->setHeaderLabels ({ tr ("Property"), tr ("Value") });
    xmpTree->header()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    xmpTree->setEditTriggers (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    layout->addWidget (xmpTree, 1);
    auto* note = new QLabel (tr ("XMP is kept inside WAV, AIFF, MP3 and M4A files, and in a .xmp file beside the others. Greyed values follow "
                                 "the Common tab; editing one gives XMP its own value. Structures (history, Audition's markers) are kept as written."));
    note->setWordWrap (true);
    layout->addWidget (note);

    std::set<std::string> groups;
    for (const auto* f : meta::fieldsOf (meta::Schema::xmp))
        if (groups.insert (f->group).second)
        {
            auto* action = displayMenu->addAction (qs (f->group));
            action->setCheckable (true);
            action->setChecked (! appSettings().value (QStringLiteral ("metadata/xmpHidden/") + qs (f->group), false).toBool());
            connect (action, &QAction::toggled, this, [this, g = f->group] (bool on)
            {
                appSettings().setValue (QStringLiteral ("metadata/xmpHidden/") + qs (g), ! on);
                refreshXmp();
            });
        }

    connect (xmpSearch, &QLineEdit::textChanged, this, [this] { refreshXmp(); });
    connect (xmpTree, &QTreeWidget::itemChanged, this, [this] (QTreeWidgetItem* item, int column)
    {
        if (updating || column != 1)
            return;
        const auto key = item->data (0, Qt::UserRole).toString().toStdString();
        if (key.empty())
            return;
        const auto text = item->text (1).toStdString();
        const auto* f = meta::find (key);
        Tags without = current;
        without.erase (key);
        if (text.empty() || (f != nullptr && ! f->common.empty() && text == meta::get (without, *f)))
            current.erase (key);
        else
            current[key] = text;
        refreshAll();
    });
    connect (add, &QPushButton::clicked, this, [this]
    {
        bool ok = false;
        const auto prefix = QInputDialog::getText (this, tr ("Add XMP Property"), tr ("Namespace prefix (dc, xmpDM, or your own):"),
                                                   QLineEdit::Normal, QStringLiteral ("xmpDM"), &ok).trimmed();
        if (! ok || prefix.isEmpty())
            return;
        auto uri = qs (xmp::uriOf (prefix.toStdString()));
        if (uri.isEmpty())
            uri = qs (valueOf (current, "xmpns:" + prefix.toStdString()));
        if (uri.isEmpty())
        {
            uri = QInputDialog::getText (this, tr ("Add XMP Property"), tr ("Namespace URI for %1:").arg (prefix), QLineEdit::Normal,
                                         QStringLiteral ("http://example.com/%1/").arg (prefix), &ok).trimmed();
            if (! ok || uri.isEmpty())
                return;
            current["xmpns:" + prefix.toStdString()] = uri.toStdString();
        }
        const auto name = QInputDialog::getText (this, tr ("Add XMP Property"), tr ("Property name:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (! ok || name.isEmpty())
            return;
        const auto value = QInputDialog::getText (this, tr ("Add XMP Property"), tr ("Value:"), QLineEdit::Normal, {}, &ok);
        if (! ok)
            return;
        current["xmp:" + prefix.toStdString() + ":" + name.toStdString()] = value.toStdString();
        refreshAll();
    });

    tabs->addTab (page, tr ("&XMP"));
}

void MetadataEditor::refreshXmp()
{
    if (xmpTree == nullptr)
        return;
    xmpDirty = false;
    const QSignalBlocker block (xmpTree);
    const auto filter = xmpSearch->text().trimmed();
    std::map<std::string, bool> expanded;
    for (int i = 0; i < xmpTree->topLevelItemCount(); ++i)
        expanded[xmpTree->topLevelItem (i)->text (0).toStdString()] = xmpTree->topLevelItem (i)->isExpanded();
    const auto scroll = xmpTree->verticalScrollBar()->value();
    xmpTree->clear();

    std::map<std::string, QTreeWidgetItem*> groups;
    const auto groupItem = [&] (const std::string& name)
    {
        auto*& item = groups[name];
        if (item == nullptr)
        {
            item = new QTreeWidgetItem (xmpTree, { qs (name) });
            item->setFirstColumnSpanned (true);
            auto font = item->font (0);
            font.setBold (true);
            item->setFont (0, font);
            item->setExpanded (expanded.count (name) == 0 || expanded[name] || ! filter.isEmpty());
        }
        return item;
    };
    const auto matches = [&] (const QString& a, const QString& b, const QString& c)
    {
        return filter.isEmpty() || a.contains (filter, Qt::CaseInsensitive) || b.contains (filter, Qt::CaseInsensitive) || c.contains (filter, Qt::CaseInsensitive);
    };

    for (const auto* f : meta::fieldsOf (meta::Schema::xmp))
    {
        if (appSettings().value (QStringLiteral ("metadata/xmpHidden/") + qs (f->group), false).toBool())
            continue;
        const bool own = current.count (f->key) > 0;
        const auto value = qs (meta::get (current, *f));
        if (! matches (qs (f->label), qs (f->id), value))
            continue;
        auto* item = new QTreeWidgetItem (groupItem (f->group), { qs (f->label) + QStringLiteral ("  (") + qs (f->id) + QLatin1Char (')'), value });
        item->setData (0, Qt::UserRole, qs (f->key));
        item->setFlags (item->flags() | Qt::ItemIsEditable);
        if (! own && ! value.isEmpty())
        {
            item->setForeground (1, palette().color (QPalette::PlaceholderText));
            item->setToolTip (1, tr ("Follows the Common %1").arg (commonLabel (f->common)));
        }
        else if (own && ! f->common.empty() && current.count (f->common) > 0)
            item->setText (0, item->text (0) + QStringLiteral (" \xE2\x89\xA0"));
    }
    for (const auto& [key, value] : current)
    {
        if (key.compare (0, 4, "xmp:") == 0 && meta::find (key) == nullptr)
        {
            if (! matches (qs (key.substr (4)), {}, qs (value)))
                continue;
            auto* item = new QTreeWidgetItem (groupItem ("Other namespaces"), { qs (key.substr (4)), qs (value) });
            item->setData (0, Qt::UserRole, qs (key));
            item->setFlags (item->flags() | Qt::ItemIsEditable);
        }
        else if (key.compare (0, 7, "xmpraw:") == 0 && matches (qs (key.substr (7)), {}, {}))
        {
            auto first = qs (value).simplified();
            auto* item = new QTreeWidgetItem (groupItem ("Structures (kept as written)"), { qs (key.substr (7)), first.left (120) + QStringLiteral ("...") });
            item->setToolTip (1, qs (value).left (4000));
        }
    }
    xmpTree->verticalScrollBar()->setValue (scroll);
}

void MetadataEditor::buildPictures (QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QHBoxLayout (page);
    auto* left = new QVBoxLayout;
    pictureList = new QListWidget;
    pictureList->setIconSize (QSize (64, 64));
    left->addWidget (pictureList, 1);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton (tr ("&Add..."));
    auto* replace = new QPushButton (tr ("Re&place..."));
    auto* remove = new QPushButton (tr ("&Remove"));
    auto* saveAs = new QPushButton (tr ("Save &Image..."));
    for (auto* b : { add, replace, remove, saveAs })
        row->addWidget (b);
    left->addLayout (row);
    auto* form = new QFormLayout;
    auto* type = new QComboBox;
    for (int t = 0; t <= 20; ++t)
        type->addItem (QString::fromLatin1 (meta::pictureTypeName (t)), t);
    auto* description = new QLineEdit;
    form->addRow (tr ("&Type:"), type);
    form->addRow (tr ("&Description:"), description);
    left->addLayout (form);
    auto* note = new QLabel (tr ("Pictures are kept in MP3, FLAC, Ogg, Opus, M4A, WavPack, AIFF and (with ID3 on the Write tab) WAV. "
                                 "JPEG or PNG, ideally square, 1400 pixels or more for streaming services."));
    note->setWordWrap (true);
    left->addWidget (note);
    layout->addLayout (left, 1);
    picturePreview = new QLabel;
    picturePreview->setMinimumSize (260, 260);
    picturePreview->setAlignment (Qt::AlignCenter);
    picturePreview->setFrameShape (QFrame::StyledPanel);
    layout->addWidget (picturePreview);

    const auto choose = [this] () -> std::string
    {
        const auto file = QFileDialog::getOpenFileName (this, tr ("Choose a Picture"), QFileInfo (path).absolutePath(),
                                                        tr ("Images (*.jpg *.jpeg *.png *.gif *.webp)"));
        if (file.isEmpty())
            return {};
        QFile in (file);
        if (! in.open (QIODevice::ReadOnly))
            return {};
        auto bytes = in.readAll().toStdString();
        if (meta::mimeOfImage (bytes).empty())
        {
            // Other image types are stored as PNG.
            QImage image;
            image.loadFromData (QByteArray::fromStdString (bytes));
            QByteArray png;
            QBuffer buffer (&png);
            buffer.open (QIODevice::WriteOnly);
            image.save (&buffer, "PNG");
            bytes = png.toStdString();
        }
        return bytes;
    };
    connect (add, &QPushButton::clicked, this, [this, choose]
    {
        auto bytes = choose();
        if (bytes.empty())
            return;
        auto list = meta::pictures (current);
        meta::Picture p;
        p.type = list.empty() ? 3 : 0;
        p.data = std::move (bytes);
        p.mime = meta::mimeOfImage (p.data);
        list.push_back (std::move (p));
        meta::setPictures (current, list);
        refreshPictures();
        pictureList->setCurrentRow (pictureList->count() - 1);
    });
    connect (replace, &QPushButton::clicked, this, [this, choose]
    {
        const auto row = pictureList->currentRow();
        auto list = meta::pictures (current);
        if (row < 0 || row >= (int) list.size())
            return;
        auto bytes = choose();
        if (bytes.empty())
            return;
        list[(size_t) row].data = std::move (bytes);
        list[(size_t) row].mime = meta::mimeOfImage (list[(size_t) row].data);
        meta::setPictures (current, list);
        refreshPictures();
        pictureList->setCurrentRow (row);
    });
    connect (remove, &QPushButton::clicked, this, [this]
    {
        const auto row = pictureList->currentRow();
        auto list = meta::pictures (current);
        if (row < 0 || row >= (int) list.size())
            return;
        list.erase (list.begin() + row);
        meta::setPictures (current, list);
        refreshPictures();
    });
    connect (saveAs, &QPushButton::clicked, this, [this]
    {
        const auto row = pictureList->currentRow();
        const auto list = meta::pictures (current);
        if (row < 0 || row >= (int) list.size())
            return;
        const auto& p = list[(size_t) row];
        const auto suffix = p.mime == "image/png" ? QStringLiteral ("png") : QStringLiteral ("jpg");
        const auto file = QFileDialog::getSaveFileName (this, tr ("Save Picture"), QFileInfo (path).absolutePath() + QStringLiteral ("/cover.") + suffix);
        if (file.isEmpty())
            return;
        QFile out (file);
        if (out.open (QIODevice::WriteOnly))
            out.write (p.data.data(), (qint64) p.data.size());
    });
    connect (pictureList, &QListWidget::currentRowChanged, this, [this, type, description] (int row)
    {
        const auto list = meta::pictures (current);
        const QSignalBlocker b1 (type), b2 (description);
        if (row < 0 || row >= (int) list.size())
        {
            picturePreview->clear();
            return;
        }
        const auto& p = list[(size_t) row];
        type->setCurrentIndex (std::clamp (p.type, 0, 20));
        description->setText (qs (p.description));
        QImage image;
        image.loadFromData (QByteArray::fromStdString (p.data));
        picturePreview->setPixmap (QPixmap::fromImage (image).scaled (256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        picturePreview->setToolTip (tr ("%1 x %2 pixels, %3 KB").arg (image.width()).arg (image.height()).arg ((int) (p.data.size() / 1024)));
    });
    const auto edit = [this, type, description]
    {
        const auto row = pictureList->currentRow();
        auto list = meta::pictures (current);
        if (row < 0 || row >= (int) list.size())
            return;
        list[(size_t) row].type = type->currentData().toInt();
        list[(size_t) row].description = description->text().toStdString();
        meta::setPictures (current, list);
        refreshPictures();
        pictureList->setCurrentRow (row);
    };
    connect (type, &QComboBox::activated, this, edit);
    connect (description, &QLineEdit::editingFinished, this, edit);

    tabs->addTab (page, tr ("&Pictures"));
}

void MetadataEditor::refreshPictures()
{
    if (pictureList == nullptr)
        return;
    const auto row = pictureList->currentRow();
    pictureList->blockSignals (true);
    pictureList->clear();
    for (const auto& p : meta::pictures (current))
    {
        QImage image;
        image.loadFromData (QByteArray::fromStdString (p.data));
        auto* item = new QListWidgetItem (QIcon (QPixmap::fromImage (image.scaled (64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                          tr ("%1, %2 x %3").arg (QString::fromLatin1 (meta::pictureTypeName (p.type))).arg (image.width()).arg (image.height()),
                                          pictureList);
        item->setToolTip (qs (p.description));
    }
    pictureList->blockSignals (false);
    pictureList->setCurrentRow (std::min (std::max (row, 0), pictureList->count() - 1));
}

void MetadataEditor::buildRaw (QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout (page);
    auto* fileTree = new QTreeWidget;
    fileTree->setHeaderLabels ({ tr ("In the file on disk"), tr ("Size / value") });
    fileTree->header()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
    layout->addWidget (new QLabel (tr ("Every chunk, frame or atom in the saved file:")));
    layout->addWidget (fileTree, 2);

    const auto file = path.toStdString();
    if (! path.isEmpty() && QFileInfo::exists (path))
    {
        const auto layoutInfo = riff::scan (file);
        if (layoutInfo.valid)
        {
            for (const auto& c : layoutInfo.chunks)
            {
                const auto name = qs (c.id) + (c.listType.empty() ? QString() : QStringLiteral (" ") + qs (c.listType));
                new QTreeWidgetItem (fileTree, { name, tr ("%1 bytes at %2").arg ((qulonglong) c.size).arg ((qulonglong) c.offset) });
            }
        }
        else
            for (const auto& [name, value] : tagio::rawItems (file))
                new QTreeWidgetItem (fileTree, { qs (name), qs (value) });
    }
    else
        new QTreeWidgetItem (fileTree, { tr ("Not saved yet") });

    layout->addWidget (new QLabel (tr ("Kept from the file and written back unchanged (delete what you do not want):")));
    keptList = new QListWidget;
    layout->addWidget (keptList, 1);
    auto* remove = new QPushButton (tr ("&Delete"));
    layout->addWidget (remove, 0, Qt::AlignLeft);
    connect (remove, &QPushButton::clicked, this, [this]
    {
        if (auto* item = keptList->currentItem())
        {
            current.erase (item->data (Qt::UserRole).toString().toStdString());
            refreshAll();
        }
    });
    refreshers.push_back ([this]
    {
        keptList->clear();
        for (const auto& [key, value] : current)
            if (key.compare (0, 8, "raw:wav:") == 0 || key.compare (0, 7, "xmpraw:") == 0)
            {
                const auto bytes = key[0] == 'r' ? meta::base64Decode (value).size() : value.size();
                auto* item = new QListWidgetItem (key[0] == 'r' ? tr ("WAV chunk \"%1\", %2 bytes").arg (qs (key.substr (8))).arg ((qulonglong) bytes)
                                                                : tr ("XMP structure %1, %2 bytes").arg (qs (key.substr (7))).arg ((qulonglong) bytes),
                                                  keptList);
                item->setData (Qt::UserRole, qs (key));
            }
    });
    tabs->addTab (page, tr ("Ra&w"));
}

void MetadataEditor::buildWrite (QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout (page);
    layout->addWidget (new QLabel (tr ("Saving as .%1 writes:").arg (extension)));

    const auto schemas = AudioEngine::schemasOf (extension.toStdString());
    const auto has = [&] (meta::Schema s) { return std::find (schemas.begin(), schemas.end(), s) != schemas.end(); };
    const auto box = [&] (const QString& label, bool& flag, bool shown)
    {
        auto* check = new QCheckBox (label);
        check->setChecked (flag);
        check->setVisible (shown);
        connect (check, &QCheckBox::toggled, this, [&flag] (bool on) { flag = on; });
        layout->addWidget (check);
        return check;
    };
    const bool wav = extension == QStringLiteral ("wav");
    const bool aiff = extension.startsWith (QStringLiteral ("aif"));
    box (aiff ? tr ("AIFF name, author, copyright and annotation") : tr ("RIFF INFO and Display Title"), writeOptions.riff, wav || aiff);
    box (tr ("Broadcast Wave (bext)"), writeOptions.bext, wav);
    box (tr ("Cart chunk"), writeOptions.cart, wav);
    box (tr ("iXML"), writeOptions.ixml, wav);
    box (wav ? tr ("ID3 (an \"id3\" chunk, read by many players)") : tr ("ID3v2"), writeOptions.id3, has (meta::Schema::id3));
    box (tr ("ID3v1 as well (MP3)"), writeOptions.id3v1, extension == QStringLiteral ("mp3"));
    auto* version = new QComboBox;
    version->addItem (tr ("ID3v2.4 (UTF-8)"), 4);
    version->addItem (tr ("ID3v2.3 (for older players)"), 3);
    version->setCurrentIndex (writeOptions.id3Version == 3 ? 1 : 0);
    version->setVisible (has (meta::Schema::id3));
    connect (version, &QComboBox::activated, this, [this, version] { writeOptions.id3Version = version->currentData().toInt(); });
    layout->addWidget (version, 0, Qt::AlignLeft);
    box (tr ("Vorbis comments and pictures"), writeOptions.vorbis, has (meta::Schema::vorbis));
    box (tr ("MP4 / iTunes atoms"), writeOptions.mp4, has (meta::Schema::mp4));
    box (tr ("APEv2"), writeOptions.ape, has (meta::Schema::ape));
    box (tr ("XMP"), writeOptions.xmp, has (meta::Schema::xmp));
    box (wav ? tr ("Markers as cue points and regions") : tr ("Markers as chapters"), writeOptions.chapters, true);
    auto* remembered = new QLabel (tr ("Remembered for every .%1 file. Export has its own choice per output.").arg (extension));
    remembered->setWordWrap (true);
    layout->addWidget (remembered);

    auto* strip = new QPushButton (tr ("Strip &All Metadata"));
    strip->setToolTip (tr ("Removes every tag, picture and kept chunk (markers stay)"));
    connect (strip, &QPushButton::clicked, this, [this]
    {
        if (QMessageBox::question (this, tr ("Strip All Metadata"), tr ("Remove every tag, picture and kept chunk from this file?")) == QMessageBox::Yes)
        {
            current.clear();
            refreshAll();
        }
    });
    layout->addWidget (strip, 0, Qt::AlignLeft);

    layout->addWidget (new QLabel (tr ("Schemas that differ from the Common values:")));
    conflictList = new QListWidget;
    layout->addWidget (conflictList, 1);
    auto* sync = new QPushButton (tr ("Sync All Schemas"));
    connect (sync, &QPushButton::clicked, this, [this] { meta::sync (current); refreshAll(); });
    layout->addWidget (sync, 0, Qt::AlignLeft);
    tabs->addTab (scrolling (page), tr ("Wri&te"));
}

void MetadataEditor::refreshConflicts()
{
    if (conflictList == nullptr)
        return;
    conflictList->clear();
    for (const auto& c : meta::conflicts (current))
    {
        const auto* f = meta::find (c.key);
        conflictList->addItem (tr ("%1 %2: \"%3\" (Common %4: \"%5\")")
                                   .arg (QString::fromLatin1 (meta::schemaName (f->schema)), qs (f->id), qs (c.value).left (60),
                                         commonLabel (c.common), qs (c.commonValue).left (60)));
    }
    if (conflictList->count() == 0)
        conflictList->addItem (tr ("None: every schema follows the Common values."));
}

void MetadataEditor::refreshAll()
{
    updating = true;
    const auto conflicts = meta::conflicts (current);
    for (auto& b : bound)
    {
        if (b.editor->hasFocus() && b.editor->isVisible())
            continue;   // the one being typed in
        if (b.field == nullptr)
        {
            setEditor (b.editor, qs (valueOf (current, b.key)), {});
            // Mark common keys some schema disagrees with.
            QStringList differs;
            for (const auto& c : conflicts)
                if (c.common == b.key)
                    differs << QStringLiteral ("%1 %2: %3").arg (QString::fromLatin1 (meta::schemaName (meta::find (c.key)->schema)),
                                                                 qs (meta::find (c.key)->id), qs (c.value).left (60));
            b.label->setText (commonLabel (b.key) + (differs.isEmpty() ? QStringLiteral (":") : QStringLiteral (" \xE2\x89\xA0:")));
            b.label->setToolTip (differs.isEmpty() ? QString() : tr ("Differs in %1").arg (differs.join (QStringLiteral ("; "))));
        }
        else
        {
            const auto own = current.count (b.key) > 0;
            Tags without = current;
            without.erase (b.key);
            const auto follows = b.field->common.empty() ? std::string() : meta::get (without, *b.field);
            setEditor (b.editor, own ? qs (current.at (b.key)) : QString(), qs (follows));
            const bool conflict = own && ! follows.empty() && follows != current.at (b.key);
            b.label->setText (QStringLiteral ("%1 <span style='color:gray'>%2</span>%3:")
                                  .arg (qs (b.field->label).toHtmlEscaped(), qs (b.field->id).toHtmlEscaped(),
                                        conflict ? QStringLiteral (" \xE2\x89\xA0") : QString()));
        }
    }
    for (auto& r : refreshers)
        r();
    // Pictures are decoded again only when they changed.
    std::string signature;
    for (auto it = current.lower_bound ("picture."); it != current.end() && it->first.compare (0, 8, "picture.") == 0; ++it)
        signature += it->first + std::to_string (it->second.size()) + it->second.substr (0, 64);
    if (signature != pictureSignature || pictureList->count() == 0)
    {
        pictureSignature = signature;
        refreshPictures();
    }
    xmpDirty = true;
    if (xmpTree->isVisible())
        refreshXmp();
    refreshConflicts();
    updating = false;
}

//==============================================================================
void MetadataEditor::applyTemplate()
{
    const auto names = templateNames();
    if (names.isEmpty())
    {
        QMessageBox::information (this, tr ("Templates"), tr ("No templates yet. Fill in the fields, then use Templates > Save as Template."));
        return;
    }
    bool ok = false;
    const auto name = QInputDialog::getItem (this, tr ("Apply a Template"), tr ("Template:"), names, 0, false, &ok);
    if (! ok)
        return;
    const auto now = QDateTime::currentDateTime();
    const std::vector<std::pair<std::string, std::string>> vars {
        { "filename", QFileInfo (path).completeBaseName().toStdString() },
        { "today", now.toString (QStringLiteral ("yyyy-MM-dd")).toStdString() },
        { "year", now.toString (QStringLiteral ("yyyy")).toStdString() },
    };
    // Variables known only at export ({lufs}, {part}...) stay as written.
    const auto text = templateText (name).toStdString();
    size_t start = 0;
    while (start < text.size())
    {
        auto nl = text.find ('\n', start);
        const auto line = text.substr (start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? text.size() : nl + 1;
        const auto eq = line.find ('=');
        if (eq == std::string::npos || eq == 0 || line[0] == '#')
            continue;
        const auto value = meta::expand (line.substr (eq + 1), current, vars);
        if (value.empty())
            current.erase (line.substr (0, eq));
        else
            current[line.substr (0, eq)] = value;
    }
    refreshAll();
}

void MetadataEditor::saveAsTemplate()
{
    QString text;
    for (const auto& [key, value] : current)
        if (key.compare (0, 8, "picture.") != 0 && key.compare (0, 4, "raw:") != 0 && key.compare (0, 7, "xmpraw:") != 0
            && value.find ('\n') == std::string::npos)
            text += qs (key) + QLatin1Char ('=') + qs (value) + QLatin1Char ('\n');

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Save as Template"));
    auto* name = new QLineEdit (&dialog);
    auto* edit = new QPlainTextEdit (text, &dialog);
    auto* help = new QLabel (tr ("One field per line, as key=value. A value can use variables, such as title={filename} or "
                                 "copyright=\xC2\xA9 {year} {artist}.\n") + variablesHelp(), &dialog);
    help->setWordWrap (true);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* form = new QFormLayout (&dialog);
    form->addRow (tr ("&Name:"), name);
    form->addRow (tr ("&Fields:"), edit);
    form->addRow (help);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize (520, 460);
    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return;
    saveTemplate (name->text().trimmed(), edit->toPlainText());
}

void MetadataEditor::importRows()
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("Import Tags"), QFileInfo (path).absolutePath(), tr ("Spreadsheets (*.csv *.json)"));
    if (file.isEmpty())
        return;
    QFile in (file);
    if (! in.open (QIODevice::ReadOnly))
        return;
    const auto text = in.readAll().toStdString();
    std::string error;
    const auto rows = file.endsWith (QStringLiteral (".json"), Qt::CaseInsensitive) ? meta::fromJson (text, error) : meta::fromCsv (text, error);
    if (rows.empty())
    {
        QMessageBox::warning (this, tr ("Import Tags"), error.empty() ? tr ("The file has no rows.") : qs (error));
        return;
    }
    // The row for this file (by name), else the first.
    const auto* row = &rows.front();
    const auto name = QFileInfo (path).fileName();
    for (const auto& r : rows)
        if (! name.isEmpty() && QFileInfo (qs (r.file)).fileName() == name)
            row = &r;
    for (const auto& [key, value] : row->tags)
        current[key] = value;
    refreshAll();
}

void MetadataEditor::exportRows()
{
    const auto file = QFileDialog::getSaveFileName (this, tr ("Export Tags"),
                                                    QFileInfo (path).absolutePath() + QLatin1Char ('/') + QFileInfo (path).completeBaseName()
                                                        + QStringLiteral (" tags.csv"),
                                                    tr ("CSV (*.csv);;JSON (*.json)"));
    if (file.isEmpty())
        return;
    const std::vector<meta::Row> rows { { QFileInfo (path).fileName().toStdString(), current } };
    const auto text = file.endsWith (QStringLiteral (".json"), Qt::CaseInsensitive) ? meta::toJson (rows) : meta::toCsv (rows);
    QFile out (file);
    if (! out.open (QIODevice::WriteOnly | QIODevice::Truncate) || out.write (text.data(), (qint64) text.size()) < 0)
        QMessageBox::warning (this, tr ("Export Tags"), tr ("Could not write %1").arg (file));
}
