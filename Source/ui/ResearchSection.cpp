// SPDX-License-Identifier: AGPL-3.0-only
// The Research menu: quick reference guides to what is built into PHI (owner, 2026-10-29).
// Each guide is written out from the code's own lists as it opens (the effects' parameters,
// the Ambisonic Studio's device specs, the command registry), so it always matches the build.
#include "MainWindow.h"
#include "../core/SpatialStudio.h"
#include "../engine/BuiltinEffects.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMap>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    QString esc (const QString& s)   { return s.toHtmlEscaped(); }
    QString esc (const std::string& s) { return QString::fromStdString (s).toHtmlEscaped(); }
    QString esc (const juce::String& s) { return QString::fromUtf8 (s.toRawUTF8()).toHtmlEscaped(); }

    QString tableStart (const QStringList& headings)
    {
        QString html = QStringLiteral ("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\" width=\"100%\"><tr>");
        for (const auto& h : headings)
            html += QStringLiteral ("<th align=\"left\">%1</th>").arg (esc (h));
        return html + QStringLiteral ("</tr>");
    }

    QString row (const QStringList& cells)
    {
        QString html = QStringLiteral ("<tr>");
        for (const auto& c : cells)
            html += QStringLiteral ("<td valign=\"top\">%1</td>").arg (c);
        return html + QStringLiteral ("</tr>");
    }

    // A value as the effect shows it, with its unit when the text leaves it out.
    QString valueText (juce::AudioProcessorParameter& p, float normalised)
    {
        auto text = p.getText (normalised, 64).trim();
        const auto label = p.getLabel().trim();
        if (label.isNotEmpty() && ! text.endsWith (label))
            text << " " << label;
        return esc (text);
    }

    QString effectsGuide()
    {
        // By category, as Add plugin lists them under Built-in.
        QMap<QString, QString> byCategory;
        for (const auto& d : builtin::descriptions())
        {
            auto effect = builtin::create (d);
            if (effect == nullptr)
                continue;
            auto& html = byCategory[QString::fromUtf8 (d.category.toRawUTF8())];
            html += QStringLiteral ("<h3>%1</h3>").arg (esc (d.name));
            const auto& params = effect->getParameters();
            if (params.isEmpty())
            {
                html += QStringLiteral ("<p>%1</p>").arg (QObject::tr ("No parameters."));
                continue;
            }
            html += tableStart ({ QObject::tr ("Parameter"), QObject::tr ("Range"), QObject::tr ("Default") });
            for (auto* p : params)
            {
                const auto name = esc (p->getName (64));
                QString range;
                if (p->isBoolean())
                    range = QObject::tr ("Off or On");
                else if (const auto choices = p->getAllValueStrings(); p->isDiscrete() && ! choices.isEmpty() && choices.size() <= 16)
                {
                    QStringList list;
                    for (const auto& c : choices)
                        list << esc (c);
                    range = list.join (QStringLiteral (", "));
                }
                else
                    range = QObject::tr ("%1 to %2").arg (valueText (*p, 0.0f), valueText (*p, 1.0f));
                html += row ({ QStringLiteral ("<b>%1</b>").arg (name), range, valueText (*p, p->getDefaultValue()) });
            }
            html += QStringLiteral ("</table>");
        }

        QString html = QStringLiteral ("<h1>%1</h1><p>%2</p>")
                           .arg (QObject::tr ("Built-in Effects"),
                                 QObject::tr ("Every effect that ships inside PHI, with each parameter's range and default. "
                                              "Add one from the Master Section or a mixer track's inserts: Add plugin, then Built-in."));
        for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it)
            html += QStringLiteral ("<h2>%1</h2>").arg (esc (it.key())) + it.value();
        return html;
    }

    QString number (double v, int decimals)
    {
        return QString::number (v, 'f', std::max (0, decimals));
    }

    QString spatialGuide()
    {
        QString html = QStringLiteral ("<h1>%1</h1><p>%2</p>")
                           .arg (QObject::tr ("Ambisonic Studio Devices"),
                                 QObject::tr ("The devices of Spatial > Ambisonic Studio and their controls. "
                                              "Sources place the dry input in the scene; scene devices change the scene itself."));
        for (const bool sources : { true, false })
        {
            html += QStringLiteral ("<h2>%1</h2>").arg (sources ? QObject::tr ("Sources") : QObject::tr ("On the scene"));
            for (const auto& device : studio::devices())
            {
                if (device.source != sources)
                    continue;
                html += QStringLiteral ("<h3>%1</h3><p>%2</p>").arg (esc (device.name), esc (device.description));
                if (device.params.empty())
                    continue;
                html += tableStart ({ QObject::tr ("Control"), QObject::tr ("Range"), QObject::tr ("Default"), QObject::tr ("What it does") });
                std::string group;
                for (const auto& p : device.params)
                {
                    if (p.group != group)
                    {
                        group = p.group;
                        if (! group.empty())
                            html += QStringLiteral ("<tr><td colspan=\"4\"><i>%1</i></td></tr>").arg (esc (group));
                    }
                    const auto unit = p.unit.empty() ? QString() : QStringLiteral (" ") + esc (p.unit);
                    QString range, def;
                    if (p.toggle)
                    {
                        range = QObject::tr ("Off or On");
                        def = p.def != 0.0 ? QObject::tr ("On") : QObject::tr ("Off");
                    }
                    else if (! p.choices.empty())
                    {
                        QStringList list;
                        for (const auto& c : p.choices)
                            list << esc (c);
                        range = list.join (QStringLiteral (", "));
                        const auto index = (size_t) std::max (0.0, p.def);
                        def = index < p.choices.size() ? esc (p.choices[index]) : QString();
                    }
                    else
                    {
                        range = QObject::tr ("%1 to %2").arg (number (p.min, p.decimals) + unit, number (p.max, p.decimals) + unit);
                        def = number (p.def, p.decimals) + unit;
                    }
                    html += row ({ QStringLiteral ("<b>%1</b>").arg (esc (p.label)), range, def, esc (p.help) });
                }
                html += QStringLiteral ("</table>");
            }
        }
        return html;
    }

    QString commandsGuide (const CommandRegistry& commands)
    {
        QMap<QString, QList<const Command*>> byCategory;
        for (const auto& c : commands.all())
            byCategory[c.category] << &c;

        QString html = QStringLiteral ("<h1>%1</h1><p>%2</p>")
                           .arg (QObject::tr ("Commands and Shortcuts"),
                                 QObject::tr ("Every command in PHI with its keys as they are set now "
                                              "(Options > Keyboard Shortcuts changes them; Ctrl+Shift+P finds and runs any of them)."));
        for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it)
        {
            html += QStringLiteral ("<h2>%1</h2>").arg (esc (it.key()));
            html += tableStart ({ QObject::tr ("Command"), QObject::tr ("Keys") });
            for (const auto* c : it.value())
                html += row ({ esc (c->name()), esc (c->shortcutText()) });
            html += QStringLiteral ("</table>");
        }
        return html;
    }
}

void MainWindow::registerResearchCommands()
{
    const auto category = tr ("Research");
    commands.add (QStringLiteral ("research.effects"), category, tr ("&Built-in Effects..."), {},
                  [this] { showResearchGuide (tr ("Built-in Effects"), effectsGuide()); });
    commands.add (QStringLiteral ("research.spatialStudio"), category, tr ("&Ambisonic Studio Devices..."), {},
                  [this] { showResearchGuide (tr ("Ambisonic Studio Devices"), spatialGuide()); });
    commands.add (QStringLiteral ("research.commands"), category, tr ("&Commands and Shortcuts..."), {},
                  [this] { showResearchGuide (tr ("Commands and Shortcuts"), commandsGuide (commands)); });
}

void MainWindow::showResearchGuide (const QString& title, const QString& html)
{
    // Not modal, so a guide can stay open beside the editor while you work.
    auto* dialog = new QDialog (this);
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (title);
    dialog->resize (760, 640);

    auto* find = new QLineEdit (dialog);
    find->setPlaceholderText (tr ("Type to find in this guide; Enter finds the next one"));
    find->setClearButtonEnabled (true);
    find->setAccessibleName (tr ("Find in guide"));

    auto* browser = new QTextBrowser (dialog);
    browser->setAccessibleName (title);
    browser->setOpenExternalLinks (true);
    browser->setHtml (html);

    const auto findNext = [browser, find] (bool fromTop)
    {
        if (find->text().isEmpty())
            return;
        if (fromTop)
            browser->moveCursor (QTextCursor::Start);
        if (! browser->find (find->text()))
        {
            // Wrap round to the top.
            browser->moveCursor (QTextCursor::Start);
            browser->find (find->text());
        }
    };
    connect (find, &QLineEdit::textChanged, dialog, [findNext] { findNext (true); });
    connect (find, &QLineEdit::returnPressed, dialog, [findNext] { findNext (false); });

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, dialog);
    connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    auto* layout = new QVBoxLayout (dialog);
    layout->addWidget (find);
    layout->addWidget (browser, 1);
    layout->addWidget (buttons);
    dialog->show();
}
