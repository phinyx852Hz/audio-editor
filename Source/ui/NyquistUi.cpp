// SPDX-License-Identifier: AGPL-3.0-only
#include "NyquistUi.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <cmath>
#include <set>

namespace nyquistui
{
    namespace
    {
        // The share folder: next to the installed binary, else the source tree.
        QString shareFolder()
        {
            const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHINYX_SHARE_RELATIVE));
            if (QFileInfo::exists (installed + QStringLiteral ("/nyquist/init.lsp")))
                return QDir::cleanPath (installed);
            return QStringLiteral (PHINYX_SHARE_SOURCE_DIR);
        }

        QString settingsKey (const QString& key, const QString& var)
        {
            return QStringLiteral ("nyquist/%1/%2").arg (key, var);
        }
    }

    QString userFolder()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/plug-ins");
    }

    QStringList setUpFolders()
    {
        QDir().mkpath (userFolder());
        const auto share = shareFolder();
        const auto home = QDir::homePath();
        QStringList folders { userFolder(), share + QStringLiteral ("/nyquist-plug-ins") };
        // An installed Audacity's plug-ins (its own and ones added to it).
        for (const auto& f : { QStringLiteral ("/usr/share/audacity/plug-ins"), QStringLiteral ("/usr/local/share/audacity/plug-ins"),
                               home + QStringLiteral ("/.local/share/audacity/Plug-Ins"), home + QStringLiteral ("/.audacity-data/Plug-Ins"),
                               home + QStringLiteral ("/.audacity-files/plug-ins") })
            if (QFileInfo (f).isDir())
                folders << f;

        std::vector<std::string> plugins;
        for (const auto& f : folders)
            plugins.push_back (f.toStdString());
        nyquist::setFolders ((share + QStringLiteral ("/nyquist")).toStdString(), plugins, userFolder().toStdString());
        return folders;
    }

    std::vector<Entry> scan()
    {
        std::vector<Entry> entries;
        std::set<QString> seen;
        for (const auto& folder : setUpFolders())
        {
            QDirIterator it (folder, { QStringLiteral ("*.ny"), QStringLiteral ("*.NY") }, QDir::Files);
            QStringList files;
            while (it.hasNext())
                files << it.next();
            files.sort();
            for (const auto& path : files)
            {
                const auto base = QFileInfo (path).completeBaseName().toLower();
                if (seen.count (base) > 0)
                    continue;
                auto header = nyquist::load (path.toStdString());
                if (! header.ok || header.name.empty())
                    continue;
                seen.insert (base);
                header.code.clear();   // read again when it runs
                entries.push_back ({ QStringLiteral ("nyquist.") + base, path, std::move (header) });
            }
        }
        return entries;
    }

    void restoreValues (const QString& key, nyquist::Script& script)
    {
        QSettings settings;
        for (auto& c : script.controls)
        {
            const auto v = settings.value (settingsKey (key, QString::fromStdString (c.var)));
            if (! v.isValid())
                continue;
            using T = nyquist::Control::Type;
            if (c.type == T::string || c.type == T::file)
                c.text = v.toString().toStdString();
            else if (c.type != T::text)
                c.value = std::clamp (v.toDouble(), c.low, c.high);
        }
    }

    void storeValues (const QString& key, const nyquist::Script& script)
    {
        QSettings settings;
        for (const auto& c : script.controls)
        {
            using T = nyquist::Control::Type;
            const auto k = settingsKey (key, QString::fromStdString (c.var));
            if (c.type == T::string || c.type == T::file)
                settings.setValue (k, QString::fromStdString (c.text));
            else if (c.type != T::text)
                settings.setValue (k, c.value);
        }
    }

    void showOutput (QWidget* parent, const QString& title, const QString& text)
    {
        QDialog dialog (parent);
        dialog.setWindowTitle (title);
        auto* view = new QPlainTextEdit (&dialog);
        view->setReadOnly (true);
        view->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
        view->setPlainText (text.isEmpty() ? QObject::tr ("(Nyquist printed nothing.)") : text);
        auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, &dialog);
        QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto* layout = new QVBoxLayout (&dialog);
        layout->addWidget (view);
        layout->addWidget (buttons);
        dialog.resize (640, 420);
        dialog.exec();
    }
}

//==============================================================================
NyquistDialog::NyquistDialog (nyquist::Script& s, QWidget* parent)
    : QDialog (parent), script (s)
{
    setWindowTitle (QString::fromStdString (script.name));
    auto* form = new QFormLayout;

    for (auto& control : script.controls)
    {
        using T = nyquist::Control::Type;
        auto* c = &control;
        const auto name = QString::fromStdString (c->name);
        const auto unit = QString::fromStdString (c->label);

        switch (c->type)
        {
            case T::text:
            {
                auto* label = new QLabel (unit, this);
                label->setWordWrap (true);
                form->addRow (label);
                break;
            }
            case T::choice:
            {
                auto* box = new QComboBox (this);
                for (const auto& choice : c->choices)
                    box->addItem (QString::fromStdString (choice));
                box->setCurrentIndex ((int) std::lround (c->value));
                form->addRow (name + QLatin1Char (':'), box);
                readers.push_back ([c, box] { c->value = box->currentIndex(); });
                break;
            }
            case T::string:
            {
                auto* edit = new QLineEdit (QString::fromStdString (c->text), this);
                form->addRow (name + QLatin1Char (':'), edit);
                if (! unit.isEmpty())
                    edit->setToolTip (unit);
                readers.push_back ([c, edit] { c->text = edit->text().toStdString(); });
                break;
            }
            case T::file:
            {
                auto* edit = new QLineEdit (QString::fromStdString (c->text), this);
                auto* browse = new QPushButton (tr ("Browse..."), this);
                auto* row = new QHBoxLayout;
                row->addWidget (edit, 1);
                row->addWidget (browse);
                form->addRow (name + QLatin1Char (':'), row);
                const bool saving = c->fileFlags.find ("save") != std::string::npos;
                connect (browse, &QPushButton::clicked, this, [this, c, edit, saving]
                {
                    const auto start = QString::fromStdString (nyquist::resolveFilePath (edit->text().toStdString()));
                    const auto path = saving ? QFileDialog::getSaveFileName (this, QString::fromStdString (c->name), start)
                                             : QFileDialog::getOpenFileName (this, QString::fromStdString (c->name), start);
                    if (! path.isEmpty())
                        edit->setText (path);
                });
                readers.push_back ([c, edit] { c->text = nyquist::resolveFilePath (edit->text().toStdString()); });
                break;
            }
            case T::integer:
            case T::intText:
            {
                auto* box = new QSpinBox (this);
                box->setRange ((int) std::max (-1.0e9, c->low), (int) std::min (1.0e9, c->high));
                box->setValue ((int) std::lround (c->value));
                if (! unit.isEmpty())
                    box->setSuffix (QLatin1Char (' ') + unit);
                auto* row = new QHBoxLayout;
                row->addWidget (box);
                if (c->type == T::integer)
                {
                    auto* slider = new QSlider (Qt::Horizontal, this);
                    slider->setRange (box->minimum(), box->maximum());
                    slider->setValue (box->value());
                    slider->setMinimumWidth (180);
                    connect (slider, &QSlider::valueChanged, box, &QSpinBox::setValue);
                    connect (box, &QSpinBox::valueChanged, slider, &QSlider::setValue);
                    row->addWidget (slider, 1);
                }
                form->addRow (name + QLatin1Char (':'), row);
                readers.push_back ([c, box] { c->value = box->value(); });
                break;
            }
            case T::real:
            case T::realText:
            case T::time:
            {
                auto* box = new QDoubleSpinBox (this);
                const auto span = c->high - c->low;
                const int decimals = span <= 1.0 ? 3 : span <= 100.0 ? 2 : 1;
                box->setDecimals (c->type == T::time ? 3 : decimals);
                box->setRange (c->low, c->high);
                box->setSingleStep (c->type == T::time ? 0.1 : std::max (std::pow (10.0, -decimals), span / 100.0));
                box->setValue (c->value);
                if (c->type == T::time)
                    box->setSuffix (tr (" s"));
                else if (! unit.isEmpty())
                    box->setSuffix (QLatin1Char (' ') + unit);
                auto* row = new QHBoxLayout;
                row->addWidget (box);
                if (c->type == T::real && span > 0.0)
                {
                    // 1000 steps across the range, as Audacity's sliders have.
                    auto* slider = new QSlider (Qt::Horizontal, this);
                    slider->setRange (0, 1000);
                    slider->setMinimumWidth (180);
                    auto toSlider = [c, span] (double v) { return (int) std::lround ((v - c->low) / span * 1000.0); };
                    slider->setValue (toSlider (c->value));
                    connect (slider, &QSlider::valueChanged, box, [box, c, span] (int v)
                    {
                        const QSignalBlocker block (box);
                        box->setValue (c->low + span * v / 1000.0);
                    });
                    connect (box, &QDoubleSpinBox::valueChanged, slider, [slider, toSlider] (double v)
                    {
                        const QSignalBlocker block (slider);
                        slider->setValue (toSlider (v));
                    });
                    row->addWidget (slider, 1);
                }
                form->addRow (name + QLatin1Char (':'), row);
                readers.push_back ([c, box] { c->value = box->value(); });
                break;
            }
        }
    }

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (form);
    QStringList credit;
    if (! script.author.empty())
        credit << tr ("by %1").arg (QString::fromStdString (script.author));
    if (! script.copyright.empty())
        credit << QString::fromStdString (script.copyright);
    if (! credit.isEmpty())
    {
        auto* footer = new QLabel (credit.join (QStringLiteral (", ")), this);
        footer->setForegroundRole (QPalette::PlaceholderText);
        footer->setWordWrap (true);
        layout->addWidget (footer);
    }

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* debugButton = buttons->addButton (tr ("&Debug"), QDialogButtonBox::ActionRole);
    debugButton->setToolTip (tr ("Run it and show everything Nyquist printed"));
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, [this] { collect(); accept(); });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (debugButton, &QPushButton::clicked, this, [this] { debug = true; collect(); accept(); });
}

void NyquistDialog::collect()
{
    for (auto& r : readers)
        r();
}

//==============================================================================
NyquistPromptDialog::NyquistPromptDialog (QWidget* parent)
    : QDialog (parent), editor (new QPlainTextEdit (this))
{
    setWindowTitle (tr ("Nyquist Prompt"));
    editor->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
    editor->setTabChangesFocus (false);
    editor->setPlaceholderText (tr ("(mult *track* 0.5)"));
    editor->setPlainText (QSettings().value (QStringLiteral ("nyquist/prompt")).toString());
    editor->setToolTip (tr ("Nyquist code. *track* is the selection (or the whole file). Lisp starts with (, SAL uses return. Ctrl+Enter runs it."));

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button (QDialogButtonBox::Ok)->setToolTip (tr ("Run it (Ctrl+Enter)"));
    auto* loadButton = buttons->addButton (tr ("&Load..."), QDialogButtonBox::ActionRole);
    auto* saveButton = buttons->addButton (tr ("&Save..."), QDialogButtonBox::ActionRole);
    auto* debugButton = buttons->addButton (tr ("&Debug"), QDialogButtonBox::ActionRole);
    debugButton->setToolTip (tr ("Run it and show everything Nyquist printed"));

    auto* hint = new QLabel (tr ("Enter Nyquist code. <i>*track*</i> is the selection, or the whole file when nothing is selected. "
                                 "<a href=\"https://www.cs.cmu.edu/~music/nyquist/\">Nyquist manual</a>"), this);
    hint->setWordWrap (true);
    hint->setOpenExternalLinks (true);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (hint);
    layout->addWidget (editor, 1);
    layout->addWidget (buttons);
    resize (620, 380);

    auto finish = [this] (bool withDebug)
    {
        debug = withDebug;
        QSettings().setValue (QStringLiteral ("nyquist/prompt"), editor->toPlainText());
        accept();
    };
    connect (buttons, &QDialogButtonBox::accepted, this, [finish] { finish (false); });
    connect (debugButton, &QPushButton::clicked, this, [finish] { finish (true); });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Enter makes a new line in the code, so Ctrl+Enter runs it.
    for (const auto key : { Qt::Key_Return, Qt::Key_Enter })
        connect (new QShortcut (QKeySequence (Qt::CTRL | key), this), &QShortcut::activated, this, [finish] { finish (false); });
    connect (loadButton, &QPushButton::clicked, this, &NyquistPromptDialog::load);
    connect (saveButton, &QPushButton::clicked, this, &NyquistPromptDialog::save);
    editor->setFocus();
}

QString NyquistPromptDialog::code() const { return editor->toPlainText(); }

void NyquistPromptDialog::load()
{
    const auto path = QFileDialog::getOpenFileName (this, tr ("Load Nyquist Code"), nyquistui::userFolder(),
                                                    tr ("Nyquist (*.ny *.lsp *.sal);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f (path);
    if (! f.open (QIODevice::ReadOnly))
    {
        QMessageBox::warning (this, windowTitle(), f.errorString());
        return;
    }
    editor->setPlainText (QString::fromUtf8 (f.readAll()));
}

void NyquistPromptDialog::save()
{
    const auto path = QFileDialog::getSaveFileName (this, tr ("Save Nyquist Code"), nyquistui::userFolder(),
                                                    tr ("Nyquist (*.ny *.lsp *.sal);;All files (*)"));
    if (path.isEmpty())
        return;
    QSaveFile f (path);
    if (! f.open (QIODevice::WriteOnly) || f.write (editor->toPlainText().toUtf8()) < 0 || ! f.commit())
        QMessageBox::warning (this, windowTitle(), f.errorString());
}
