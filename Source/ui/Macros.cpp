// SPDX-License-Identifier: AGPL-3.0-only
#include "Macros.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace macro
{
    QString folder()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/macros");
    }

    namespace
    {
        // A file name from a macro name: letters, digits, spaces, - and _ kept.
        QString fileFor (const QString& name)
        {
            QString safe;
            for (const auto c : name)
                safe += c.isLetterOrNumber() || c == QLatin1Char (' ') || c == QLatin1Char ('-') || c == QLatin1Char ('_') ? c : QLatin1Char ('_');
            return folder() + QLatin1Char ('/') + safe.trimmed() + QStringLiteral (".phmacro");
        }

        QJsonObject toJson (const Macro& m)
        {
            QJsonArray steps;
            for (const auto& s : m.steps)
            {
                QJsonArray dialogs;
                for (const auto& d : s.dialogs)
                    dialogs.append (QJsonObject::fromVariantMap (d));
                QJsonObject step { { QStringLiteral ("command"), s.command }, { QStringLiteral ("dialogs"), dialogs } };
                if (s.ask)
                    step.insert (QStringLiteral ("ask"), true);
                steps.append (step);
            }
            return { { QStringLiteral ("name"), m.name }, { QStringLiteral ("version"), 1 }, { QStringLiteral ("steps"), steps } };
        }

        Macro fromJson (const QJsonObject& o)
        {
            Macro m;
            m.name = o.value (QStringLiteral ("name")).toString();
            for (const auto& value : o.value (QStringLiteral ("steps")).toArray())
            {
                const auto step = value.toObject();
                Step s;
                s.command = step.value (QStringLiteral ("command")).toString();
                s.ask = step.value (QStringLiteral ("ask")).toBool();
                for (const auto& d : step.value (QStringLiteral ("dialogs")).toArray())
                    s.dialogs.append (d.toObject().toVariantMap());
                if (! s.command.isEmpty())
                    m.steps.append (s);
            }
            return m;
        }
    }

    QList<Macro> loadAll()
    {
        QList<Macro> all;
        const QDir dir (folder());
        for (const auto& file : dir.entryList ({ QStringLiteral ("*.phmacro") }, QDir::Files))
        {
            QFile f (dir.filePath (file));
            if (! f.open (QIODevice::ReadOnly))
                continue;
            auto m = fromJson (QJsonDocument::fromJson (f.readAll()).object());
            if (m.name.isEmpty())
                m.name = QFileInfo (file).completeBaseName();
            all.append (m);
        }
        std::sort (all.begin(), all.end(), [] (const Macro& a, const Macro& b) { return a.name.localeAwareCompare (b.name) < 0; });
        return all;
    }

    bool save (const Macro& m, QString& error)
    {
        if (m.name.trimmed().isEmpty())
        {
            error = QObject::tr ("A macro needs a name.");
            return false;
        }
        QDir().mkpath (folder());
        QSaveFile f (fileFor (m.name));
        if (! f.open (QIODevice::WriteOnly) || f.write (QJsonDocument (toJson (m)).toJson()) < 0 || ! f.commit())
        {
            error = QObject::tr ("Could not save the macro: %1").arg (f.errorString());
            return false;
        }
        return true;
    }

    bool remove (const QString& name)
    {
        return QFile::remove (fileFor (name));
    }

    bool rename (const QString& from, const QString& to, QString& error)
    {
        for (auto m : loadAll())
            if (m.name == from)
            {
                m.name = to;
                if (! save (m, error))
                    return false;
                if (fileFor (from) != fileFor (to))
                    remove (from);
                return true;
            }
        error = QObject::tr ("There is no macro called %1.").arg (from);
        return false;
    }

    bool recordable (const QString& id)
    {
        for (const auto* prefix : { "view.", "help.", "options.", "tools.macro", "tools.script", "transport.playStop", "transport.stop", "transport.loop", "file.quit" })
            if (id.startsWith (QLatin1String (prefix)))
                return false;
        return true;
    }

    namespace
    {
        // The settings widgets of a window in a fixed order, each with a key of its kind and number.
        template <typename Visit>
        void forEachSetting (QDialog* dialog, Visit&& visit)
        {
            QMap<QString, int> counts;
            for (auto* w : dialog->findChildren<QWidget*>())
            {
                // Parts of other widgets (a spin box's or list's own text field, a scroll area's bars) are not settings.
                if (qobject_cast<QAbstractSpinBox*> (w->parentWidget()) != nullptr || qobject_cast<QComboBox*> (w->parentWidget()) != nullptr
                    || qobject_cast<QScrollBar*> (w) != nullptr)
                    continue;
                QString kind;
                if (qobject_cast<QComboBox*> (w) != nullptr)
                    kind = QStringLiteral ("list");
                else if (qobject_cast<QDoubleSpinBox*> (w) != nullptr)
                    kind = QStringLiteral ("number");
                else if (qobject_cast<QSpinBox*> (w) != nullptr)
                    kind = QStringLiteral ("whole");
                else if (qobject_cast<QAbstractSlider*> (w) != nullptr)
                    kind = QStringLiteral ("slider");
                else if (auto* b = qobject_cast<QAbstractButton*> (w); b != nullptr && b->isCheckable())
                    kind = QStringLiteral ("tick");
                else if (qobject_cast<QLineEdit*> (w) != nullptr)
                    kind = QStringLiteral ("text");
                else
                    continue;
                const auto key = QStringLiteral ("%1%2").arg (kind).arg (counts[kind]++);
                visit (w, kind, key);
            }
        }
    }

    QVariantMap capture (QDialog* dialog)
    {
        QVariantMap values;
        forEachSetting (dialog, [&] (QWidget* w, const QString& kind, const QString& key)
        {
            if (kind == QLatin1String ("list"))
            {
                auto* c = static_cast<QComboBox*> (w);
                values[key] = c->isEditable() ? QVariant (c->currentText()) : QVariant (c->currentIndex());
            }
            else if (kind == QLatin1String ("number"))
                values[key] = static_cast<QDoubleSpinBox*> (w)->value();
            else if (kind == QLatin1String ("whole"))
                values[key] = static_cast<QSpinBox*> (w)->value();
            else if (kind == QLatin1String ("slider"))
                values[key] = static_cast<QAbstractSlider*> (w)->value();
            else if (kind == QLatin1String ("tick"))
                values[key] = static_cast<QAbstractButton*> (w)->isChecked();
            else
                values[key] = static_cast<QLineEdit*> (w)->text();
        });
        return values;
    }

    void restore (QDialog* dialog, const QVariantMap& values)
    {
        // Twice: a choice can show, hide or reset other settings.
        for (int pass = 0; pass < 2; ++pass)
            forEachSetting (dialog, [&] (QWidget* w, const QString& kind, const QString& key)
            {
                const auto it = values.find (key);
                if (it == values.end())
                    return;
                if (kind == QLatin1String ("list"))
                {
                    auto* c = static_cast<QComboBox*> (w);
                    if (it->typeId() == QMetaType::QString)
                        c->setCurrentText (it->toString());
                    else
                        c->setCurrentIndex (std::clamp (it->toInt(), 0, std::max (0, c->count() - 1)));
                }
                else if (kind == QLatin1String ("number"))
                    static_cast<QDoubleSpinBox*> (w)->setValue (it->toDouble());
                else if (kind == QLatin1String ("whole"))
                    static_cast<QSpinBox*> (w)->setValue (it->toInt());
                else if (kind == QLatin1String ("slider"))
                    static_cast<QAbstractSlider*> (w)->setValue (it->toInt());
                else if (kind == QLatin1String ("tick"))
                    static_cast<QAbstractButton*> (w)->setChecked (it->toBool());
                else
                    static_cast<QLineEdit*> (w)->setText (it->toString());
            });
    }
}

MacroRecorder::MacroRecorder (QObject* parent) : QObject (parent)
{
    qApp->installEventFilter (this);
}

void MacroRecorder::startRecording()
{
    current = {};
    activeStep = -1;
    recording = true;
    Q_EMIT recordingChanged();
}

macro::Macro MacroRecorder::stopRecording()
{
    recording = false;
    activeStep = -1;
    Q_EMIT recordingChanged();
    return std::exchange (current, {});
}

void MacroRecorder::commandStarted (const QString& id)
{
    if (! recording || running != nullptr || ! macro::recordable (id))
        return;
    current.steps.append ({ id, false, {} });
    activeStep = (int) current.steps.size() - 1;
    Q_EMIT recordingChanged();
}

void MacroRecorder::commandFinished (const QString&)
{
    activeStep = -1;
}

void MacroRecorder::beginStep (const macro::Step& step)
{
    running = &step;
    nextDialog = 0;
}

void MacroRecorder::endStep()
{
    running = nullptr;
}

bool MacroRecorder::eventFilter (QObject* object, QEvent* event)
{
    if (event->type() != QEvent::Show || (! recording && running == nullptr))
        return false;
    auto* dialog = qobject_cast<QDialog*> (object);
    // Messages, file choosers and progress are not settings; the command palette and
    // anything marked macroSkip are not part of a step.
    if (dialog == nullptr || ! dialog->isWindow() || qobject_cast<QMessageBox*> (dialog) != nullptr || qobject_cast<QFileDialog*> (dialog) != nullptr
        || qobject_cast<QProgressDialog*> (dialog) != nullptr || dialog->property ("macroSkip").toBool())
        return false;

    if (running != nullptr)
    {
        if (nextDialog < running->dialogs.size())
            macro::restore (dialog, running->dialogs[nextDialog]);
        ++nextDialog;
        if (! running->ask)
            QTimer::singleShot (0, dialog, [dialog = QPointer<QDialog> (dialog)] { if (dialog) dialog->accept(); });
    }
    else if (activeStep >= 0)
        connect (dialog, &QDialog::finished, this, [this, dialog, step = activeStep] (int result)
        {
            if (result == QDialog::Accepted && recording && step < current.steps.size())
                current.steps[step].dialogs.append (macro::capture (dialog));
        }, Qt::SingleShotConnection);
    return false;
}
