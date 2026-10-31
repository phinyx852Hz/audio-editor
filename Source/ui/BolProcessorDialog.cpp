// SPDX-License-Identifier: AGPL-3.0-only
// Compose > South and West Asia > Bol Processor... (owner, 2026-10-31: "I would definitely want
// the full version if it's available and can be licensed with credit given to Bel/Kippen"). Runs
// Bernard Bel and Jim Kippen's BP3 console (phi-bp3, third_party/bolprocessor) on a grammar file
// and the alphabet and settings it names, shows the items it produced, and opens them in the MIDI
// editor: the bols on the tablā, or BP's own MIDI file for a grammar that makes notes.
// mixer/BolProcessor.h, docs/bol-processor.md.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/BolProcessor.h"
#include "../mixer/MidiFile.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <random>

namespace
{
    // phi-bp3 beside PHI when installed, else where the build put it.
    QString bp3Executable()
    {
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral ("phi-bp3"));
        return QFileInfo (installed).isExecutable() ? installed : QStringLiteral (PHI_BP3_BUILD_PATH);
    }

    QString bp3Strings()
    {
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/bolprocessor/console_strings.json"));
        return QFileInfo::exists (installed) ? QDir::cleanPath (installed) : QStringLiteral (PHI_BOLPROCESSOR_SOURCE_DIR "/console_strings.json");
    }

    // Kippen and Bel's qāʿida grammar, the example PHI has (share/bolprocessor).
    QString bp3Example()
    {
        const auto installed = QDir (QCoreApplication::applicationDirPath()).absoluteFilePath (QStringLiteral (PHI_SHARE_RELATIVE "/bolprocessor/examples/-gr.dhin--"));
        return QFileInfo::exists (installed) ? QDir::cleanPath (installed) : QStringLiteral (PHI_SHARE_SOURCE_DIR "/bolprocessor/-gr.dhin--");
    }
}

void MainWindow::bolProcessorDialog()
{
    showMixer();
    if (mixerWin == nullptr)
        return;
    auto settings = appSettings();
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Bol Processor"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* credit = new QLabel (tr ("The Bol Processor (BP3) by Bernard Bel and Jim Kippen, with Srikumar Karaikudi Subramanian and Anthony Kozar: "
                                   "the grammars Bel and Kippen built to model tablā improvisation. BSD licence; see bp3.tech."), &dialog);
    credit->setWordWrap (true);
    layout->addWidget (credit);
    auto* form = new QFormLayout;
    auto* row = new QHBoxLayout;
    auto* grammar = new QLineEdit (settings.value (QStringLiteral ("bolprocessor/grammar"), bp3Example()).toString(), &dialog);
    grammar->setPlaceholderText (tr ("a grammar file, -gr.name or name.bpgr"));
    auto* browse = new QPushButton (tr ("&Browse..."), &dialog);
    row->addWidget (grammar, 1);
    row->addWidget (browse);
    form->addRow (tr ("&Grammar"), row);
    auto* named = new QLabel (&dialog);
    named->setWordWrap (true);
    form->addRow (QString(), named);
    auto* seed = new QSpinBox (&dialog);
    seed->setRange (0, 999999);
    seed->setSpecialValueText (tr ("a new one each time"));
    seed->setValue (settings.value (QStringLiteral ("bolprocessor/seed"), 0).toInt());
    seed->setToolTip (tr ("The same seed produces the same items again"));
    form->addRow (tr ("&Seed"), seed);
    auto* tempo = new QSpinBox (&dialog);
    tempo->setRange (20, 400);
    tempo->setSuffix (tr (" BPM"));
    tempo->setValue (settings.value (QStringLiteral ("bolprocessor/tempo"), 60).toInt());
    tempo->setToolTip (tr ("A beat: a field between BP's periods, or one symbol at speed 1"));
    form->addRow (tr ("&Tempo"), tempo);
    auto* as = new QComboBox (&dialog);
    as->addItem (tr ("the bols on PHI tabla"), 2);
    as->addItem (tr ("the bols on General MIDI drums"), 0);
    as->addItem (tr ("BP's own MIDI file"), 1);
    as->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("bolprocessor/play"), 0).toInt(), 0, 1));
    as->setToolTip (tr ("PHI tabla: your own strokes, each bol's takes in turn; the score's track gets PHI tabla"));
    form->addRow (tr ("&Play"), as);
    layout->addLayout (form);
    auto* summary = new QLabel (&dialog);
    summary->setWordWrap (true);
    layout->addWidget (summary);
    auto* text = new QPlainTextEdit (&dialog);
    text->setReadOnly (true);
    text->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
    text->setMinimumHeight (220);
    layout->addWidget (text);

    bolprocessor::Files files;
    bolprocessor::Run produced;
    std::vector<bolprocessor::Item> items;
    auto look = [&]
    {
        files = bolprocessor::companions (grammar->text().toStdString());
        QStringList with;
        for (const auto& f : { files.alphabet, files.settings, files.soundObjects, files.tonality, files.csound })
            if (! f.empty())
                with << QFileInfo (QString::fromStdString (f)).fileName();
        named->setText (with.isEmpty() ? tr ("No alphabet or settings named in its header") : tr ("With %1").arg (with.join (QStringLiteral (", "))));
    };
    connect (grammar, &QLineEdit::editingFinished, &dialog, look);
    connect (browse, &QPushButton::clicked, &dialog, [&]
    {
        const auto file = QFileDialog::getOpenFileName (&dialog, tr ("A Bol Processor grammar"), QFileInfo (grammar->text()).absolutePath(),
                                                        tr ("Grammars (-gr.* *.bpgr);;All files (*)"));
        if (! file.isEmpty())
        {
            grammar->setText (file);
            look();
        }
    });

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* produce = buttons->addButton (tr ("P&roduce"), QDialogButtonBox::ActionRole);
    produce->setToolTip (tr ("Run the grammar: BP3 derives its items"));
    auto* open = buttons->addButton (tr ("&Open the Score"), QDialogButtonBox::AcceptRole);
    open->setEnabled (false);
    buttons->addButton (QDialogButtonBox::Close);
    layout->addWidget (buttons);
    std::random_device device;
    connect (produce, &QPushButton::clicked, &dialog, [&]
    {
        look();
        if (! QFileInfo::exists (grammar->text()))
            return summary->setText (tr ("Choose a grammar file first."));
        QApplication::setOverrideCursor (Qt::WaitCursor);
        const auto s = seed->value() > 0 ? (uint32_t) seed->value() : 1 + device() % 999999;
        produced = bolprocessor::run (bp3Executable().toStdString(), bp3Strings().toStdString(), files, s);
        QApplication::restoreOverrideCursor();
        items.clear();
        for (const auto& i : bolprocessor::splitItems (produced.items))
            items.push_back (bolprocessor::parse (i));
        if (! produced.ok)
        {
            summary->setText (tr ("The Bol Processor produced nothing; what it said is below."));
            text->setPlainText (QString::fromStdString (produced.log));
            open->setEnabled (false);
            return;
        }
        const auto score = bolprocessor::toScore (items, tempo->value());
        const bool midi = produced.midi.size() > 64;
        as->setItemData (2, midi ? QVariant() : QVariant (0), Qt::UserRole - 1);   // BP's MIDI only when it made notes
        if (! midi && as->currentIndex() == 2)
            as->setCurrentIndex (0);
        if (midi && score.parts[0].notes.empty())
            as->setCurrentIndex (2);
        summary->setText (tr ("Seed %1: %2.").arg (s).arg (QString::fromStdString (score.summary)));
        text->setPlainText (QString::fromStdString (produced.items));
        open->setEnabled (! score.parts[0].notes.empty() || midi);
    });
    connect (open, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    look();
    dialog.resize (720, 600);
    const auto accepted = dialog.exec() == QDialog::Accepted;
    settings.setValue (QStringLiteral ("bolprocessor/grammar"), grammar->text());
    settings.setValue (QStringLiteral ("bolprocessor/seed"), seed->value());
    settings.setValue (QStringLiteral ("bolprocessor/tempo"), tempo->value());
    if (as->currentIndex() < 2)
        settings.setValue (QStringLiteral ("bolprocessor/play"), as->currentIndex());
    if (! accepted)
        return;

    const auto title = QFileInfo (grammar->text()).fileName();
    if (as->currentData().toInt() == 1)
    {
        std::string why;
        if (const auto song = midifile::parse (produced.midi.data(), produced.midi.size(), why))
            return mixerWin->openScore (*song, {}, title, QStringLiteral ("phi"));
    }
    const bool tabla = as->currentData().toInt() == 2;
    const auto song = composition::toSong (bolprocessor::toScore (items, tempo->value(), tabla), title.toStdString());
    mixerWin->openScore (*song, {}, title, QStringLiteral ("phi"), tabla ? mixerWin->makePhiTabla() : nullptr);
}
