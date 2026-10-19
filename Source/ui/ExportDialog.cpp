// SPDX-License-Identifier: AGPL-3.0-only
#include "ExportDialog.h"
#include "Settings.h"

#include "MetadataEditor.h"
#include "../core/Metadata.h"
#include "../engine/AudioEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace
{
    QString qs (const std::string& s) { return QString::fromStdString (s); }

    struct LoudnessPreset
    {
        const char* name;
        double lufs;
    };

    constexpr LoudnessPreset presets[] {
        { QT_TRANSLATE_NOOP ("ExportDialog", "Spotify, YouTube, Tidal, Amazon"), -14.0 },
        { QT_TRANSLATE_NOOP ("ExportDialog", "Apple Music"), -16.0 },
        { QT_TRANSLATE_NOOP ("ExportDialog", "Podcast"), -16.0 },
        { QT_TRANSLATE_NOOP ("ExportDialog", "Broadcast, EBU R128"), -23.0 },
        { QT_TRANSLATE_NOOP ("ExportDialog", "Broadcast, ATSC A/85 (US)"), -24.0 },
    };

    QString depthName (int bits)
    {
        switch (bits)
        {
            case 31: return QObject::tr ("32-bit integer");
            case 32: return QObject::tr ("32-bit float");
            case 64: return QObject::tr ("64-bit float");
            default: return QObject::tr ("%1-bit").arg (bits);
        }
    }

}

//==============================================================================
exporter::Output ExportDialog::defaultOutput (const std::string& formatId)
{
    exporter::Output o;
    o.formatId = codec::find (formatId) != nullptr ? formatId : "wav";
    const auto* f = codec::find (o.formatId);
    o.name = f->name;
    o.settings = codec::defaults (*f);
    o.bits = f->bitDepths.empty() ? 16 : (std::find (f->bitDepths.begin(), f->bitDepths.end(), 24) != f->bitDepths.end() ? 24 : f->bitDepths.back());
    o.schemas = MetadataEditor::writeOptionsFor (qs (f->extension));
    o.schemas.software = APP_DISPLAY_NAME;
    return o;
}

std::vector<exporter::Output> ExportDialog::remembered()
{
    auto outputs = exporter::fromJson (appSettings().value (QStringLiteral ("export/outputs")).toString().toStdString());
    std::erase_if (outputs, [] (const auto& o) { return codec::find (o.formatId) == nullptr; });
    if (outputs.empty())
        outputs.push_back (defaultOutput ("wav"));
    for (auto& o : outputs)
        o.schemas.software = APP_DISPLAY_NAME;
    return outputs;
}

void ExportDialog::remember (const std::vector<exporter::Output>& outputs)
{
    appSettings().setValue (QStringLiteral ("export/outputs"), qs (exporter::toJson (outputs)));
}

ExportDialog::ExportDialog (const Context& c, QWidget* parent)
    : QDialog (parent), context (c), list (c.outputs.empty() ? remembered() : c.outputs)
{
    setWindowTitle (c.forBatch ? tr ("Batch Outputs") : tr ("Export"));
    resize (900, 700);

    // Left: the outputs.
    outputList = new QListWidget (this);
    outputList->setAccessibleName (tr ("Outputs"));
    auto* add = new QPushButton (tr ("&Add"), this);
    auto* duplicate = new QPushButton (tr ("D&uplicate"), this);
    auto* remove = new QPushButton (tr ("&Remove"), this);
    auto* presetsButton = new QToolButton (this);
    presetsButton->setText (tr ("Pre&sets"));
    presetsButton->setPopupMode (QToolButton::InstantPopup);
    auto* presetsMenu = new QMenu (presetsButton);
    presetsButton->setMenu (presetsMenu);
    connect (presetsMenu, &QMenu::aboutToShow, this, [this, presetsMenu]
    {
        presetsMenu->clear();
        presetsMenu->addAction (tr ("&Load a Preset..."), this, &ExportDialog::loadPreset);
        presetsMenu->addAction (tr ("&Save These Outputs as a Preset..."), this, &ExportDialog::savePreset);
    });

    auto* left = new QVBoxLayout;
    left->addWidget (new QLabel (tr ("Outputs (all are written):"), this));
    left->addWidget (outputList, 1);
    auto* listButtons = new QHBoxLayout;
    listButtons->addWidget (add);
    listButtons->addWidget (duplicate);
    listButtons->addWidget (remove);
    left->addLayout (listButtons);
    left->addWidget (presetsButton, 0, Qt::AlignLeft);

    // Right: the chosen output.
    auto* page = new QWidget;
    auto* form = new QFormLayout (page);
    nameEdit = new QLineEdit;
    form->addRow (tr ("&Name:"), nameEdit);

    formatBox = new QComboBox;
    formatBox->setMaxVisibleItems (24);
    auto* model = new QStandardItemModel (formatBox);
    std::string group;
    for (const auto& f : codec::formats())
    {
        if (f.group != group)
        {
            auto* header = new QStandardItem (qs (f.group));
            header->setFlags (Qt::NoItemFlags);
            auto font = header->font();
            font.setBold (true);
            header->setFont (font);
            model->appendRow (header);
            group = f.group;
        }
        auto* item = new QStandardItem (QStringLiteral ("   ") + qs (f.name) + QStringLiteral ("  (.") + qs (f.extension) + QLatin1Char (')'));
        item->setData (qs (f.id), Qt::UserRole);
        model->appendRow (item);
    }
    auto* header = new QStandardItem (tr ("Not available in this FFmpeg"));
    header->setFlags (Qt::NoItemFlags);
    model->appendRow (header);
    for (const auto& missing : codec::missingFormats())
    {
        auto* item = new QStandardItem (QStringLiteral ("   ") + qs (missing));
        item->setFlags (Qt::NoItemFlags);
        model->appendRow (item);
    }
    formatBox->setModel (model);
    form->addRow (tr ("&Format:"), formatBox);

    // The format's own settings go in rows right after it (see buildSettings).
    settingsForm = form;
    settingsRow = form->rowCount();

    rateBox = new QComboBox;
    form->addRow (tr ("Sample &rate:"), rateBox);
    depthBox = new QComboBox;
    form->addRow (tr ("&Bit depth:"), depthBox);
    ditherBox = new QComboBox;
    ditherBox->addItem (tr ("None"), (int) dsp::Dither::none);
    ditherBox->addItem (tr ("TPDF"), (int) dsp::Dither::tpdf);
    ditherBox->addItem (tr ("TPDF, noise shaped"), (int) dsp::Dither::shaped);
    form->addRow (tr ("&Dither:"), ditherBox);

    normalizeBox = new QCheckBox (tr ("&Normalize loudness to"));
    loudnessPreset = new QComboBox;
    for (const auto& p : presets)
        loudnessPreset->addItem (tr ("%1 (%2 LUFS)").arg (tr (p.name)).arg (p.lufs, 0, 'f', 0), p.lufs);
    loudnessPreset->addItem (tr ("Custom"), 0.0);
    targetBox = new QDoubleSpinBox;
    targetBox->setRange (-60.0, 0.0);
    targetBox->setDecimals (1);
    targetBox->setSingleStep (0.5);
    targetBox->setSuffix (tr (" LUFS"));
    auto* loudRow = new QHBoxLayout;
    loudRow->addWidget (loudnessPreset, 1);
    loudRow->addWidget (targetBox);
    form->addRow (normalizeBox, loudRow);
    limitBox = new QCheckBox (tr ("&Limit true peaks to"));
    ceilingBox = new QDoubleSpinBox;
    ceilingBox->setRange (-12.0, 0.0);
    ceilingBox->setDecimals (1);
    ceilingBox->setSingleStep (0.1);
    ceilingBox->setSuffix (tr (" dBTP"));
    form->addRow (limitBox, ceilingBox);

    folderEdit = new QLineEdit;
    folderEdit->setPlaceholderText (tr ("Beside the source file"));
    auto* browse = new QPushButton (tr ("Bro&wse..."));
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget (folderEdit, 1);
    folderRow->addWidget (browse);
    auto* folderLabel = new QLabel (tr ("F&older:"));
    folderLabel->setBuddy (folderEdit);
    form->addRow (folderLabel, folderRow);
    patternEdit = new QLineEdit;
    patternEdit->setToolTip (MetadataEditor::variablesHelp());
    form->addRow (tr ("File na&me:"), patternEdit);
    pathLabel = new QLabel;
    pathLabel->setWordWrap (true);
    pathLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
    form->addRow (QString(), pathLabel);
    overwriteBox = new QComboBox;
    overwriteBox->addItem (tr ("Replace it"), (int) exporter::Output::Overwrite::replace);
    overwriteBox->addItem (tr ("Add a number: Name (2)"), (int) exporter::Output::Overwrite::number);
    overwriteBox->addItem (tr ("Skip this output"), (int) exporter::Output::Overwrite::skip);
    form->addRow (tr ("If the file &exists:"), overwriteBox);

    templateBox = new QComboBox;
    form->addRow (tr ("Metadata &template:"), templateBox);
    schemaArea = new QWidget;
    new QVBoxLayout (schemaArea);
    schemaArea->layout()->setContentsMargins (0, 0, 0, 0);
    form->addRow (tr ("Write:"), schemaArea);
    fillLoudnessBox = new QCheckBox (tr ("Fill in BWF loudness (LUFS, LRA, true peak)"));
    fillLoudnessBox->setToolTip (tr ("Write the measured loudness of the exported file into the Broadcast Wave chunk"));
    form->addRow (QString(), fillLoudnessBox);
    checkPeaksBox = new QCheckBox (tr ("Check the true peak after encoding"));
    checkPeaksBox->setToolTip (tr ("Decode lossy files again and warn when encoding pushed their true peak past the ceiling"));
    form->addRow (QString(), checkPeaksBox);
    noteLabel = new QLabel;
    noteLabel->setWordWrap (true);
    form->addRow (noteLabel);

    auto* scroll = new QScrollArea (this);
    scroll->setWidget (page);
    scroll->setWidgetResizable (true);
    scroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);

    auto* columns = new QHBoxLayout;
    columns->addLayout (left, 1);
    columns->addWidget (scroll, 3);

    // Bottom: what to export, preview.
    rangeBox = new QComboBox (this);
    rangeBox->addItem (tr ("The whole file"), (int) Range::whole);
    if (context.hasSelection)
        rangeBox->addItem (tr ("The selection"), (int) Range::selection);
    if (context.regions > 0)
        rangeBox->addItem (tr ("Each region, as its own file (%1)").arg (context.regions), (int) Range::regions);
    if (context.markers > 0)
        rangeBox->addItem (tr ("Each part between markers (%1)").arg (context.markers + 1), (int) Range::markers);
    rangeBox->setCurrentIndex (std::max (0, rangeBox->findData (appSettings().value (QStringLiteral ("export/range"), 0).toInt())));
    masterBox = new QCheckBox (tr ("Through the &Master Section"), this);
    masterBox->setEnabled (context.masterActive);
    masterBox->setChecked (context.masterActive && appSettings().value (QStringLiteral ("export/throughMaster"), true).toBool());
    previewButton = new QPushButton (tr ("&Preview"), this);
    previewButton->setCheckable (true);
    previewButton->setToolTip (tr ("Hear this output encoded (the selection, or 20 seconds from the cursor); A switches between it and the original"));
    differenceBox = new QCheckBox (tr ("Only what the codec &removes"), this);
    auto* ab = new QPushButton (tr ("A/&B"), this);
    ab->setToolTip (tr ("Switch between the original and the encoded preview (the A key)"));

    auto* bottom = new QHBoxLayout;
    bottom->addWidget (new QLabel (tr ("E&xport:"), this));
    bottom->addWidget (rangeBox);
    bottom->addWidget (masterBox);
    bottom->addStretch (1);
    bottom->addWidget (previewButton);
    bottom->addWidget (ab);
    bottom->addWidget (differenceBox);
    static_cast<QLabel*> (bottom->itemAt (0)->widget())->setBuddy (rangeBox);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Cancel, this);
    auto* go = buttons->addButton (context.forBatch ? tr ("OK") : tr ("Export"), QDialogButtonBox::AcceptRole);
    go->setDefault (true);
    auto* version = new QLabel (tr ("FFmpeg %1, %2 formats").arg (qs (codec::ffmpegVersion())).arg ((int) codec::formats().size()), this);
    version->setEnabled (false);
    auto* last = new QHBoxLayout;
    last->addWidget (version);
    last->addStretch (1);
    last->addWidget (buttons);

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (columns, 1);
    layout->addLayout (bottom);
    layout->addLayout (last);

    // Wiring
    connect (outputList, &QListWidget::currentRowChanged, this, [this] (int row) { readForm(); showOutput (row); });
    connect (add, &QPushButton::clicked, this, [this]
    {
        readForm();
        list.push_back (defaultOutput ("mp3"));
        refreshList();
        outputList->setCurrentRow ((int) list.size() - 1);
        formatBox->setFocus();
    });
    connect (duplicate, &QPushButton::clicked, this, [this]
    {
        readForm();
        if (shown < 0)
            return;
        auto copy = list[(size_t) shown];
        copy.name += tr (" copy").toStdString();
        list.insert (list.begin() + shown + 1, copy);
        refreshList();
        outputList->setCurrentRow (shown + 1);
    });
    connect (remove, &QPushButton::clicked, this, [this]
    {
        if (shown < 0 || list.size() <= 1)
            return;
        list.erase (list.begin() + shown);
        shown = -1;
        refreshList();
        outputList->setCurrentRow (0);
    });
    connect (formatBox, &QComboBox::activated, this, [this]
    {
        if (shown < 0)
            return;
        readForm();
        auto& o = list[(size_t) shown];
        const auto id = formatBox->currentData().toString().toStdString();
        if (id.empty() || id == o.formatId)
            return;
        const auto* old = codec::find (o.formatId);
        const bool renamed = old == nullptr || o.name == old->name;
        const auto fresh = defaultOutput (id);
        o.formatId = id;
        o.settings = fresh.settings;
        o.schemas = fresh.schemas;
        if (renamed)
            o.name = fresh.name;
        const auto* f = codec::find (id);
        if (! f->bitDepths.empty() && std::find (f->bitDepths.begin(), f->bitDepths.end(), o.bits) == f->bitDepths.end())
            o.bits = fresh.bits;
        showOutput (shown);
        refreshList();
    });
    for (auto* w : { static_cast<QWidget*> (nameEdit), static_cast<QWidget*> (folderEdit), static_cast<QWidget*> (patternEdit) })
        connect (static_cast<QLineEdit*> (w), &QLineEdit::textEdited, this, [this] { readForm(); refreshPath(); refreshList(); });
    for (auto* box : { rateBox, depthBox, ditherBox, overwriteBox, templateBox })
        connect (box, &QComboBox::activated, this, [this] { if (! filling) { readForm(); showOutput (shown); } });
    for (auto* check : { normalizeBox, limitBox, fillLoudnessBox, checkPeaksBox })
        connect (check, &QCheckBox::toggled, this, [this] { if (! filling) { readForm(); showOutput (shown); } });
    connect (targetBox, &QDoubleSpinBox::valueChanged, this, [this] { readForm(); });
    connect (ceilingBox, &QDoubleSpinBox::valueChanged, this, [this] { readForm(); });
    connect (loudnessPreset, &QComboBox::activated, this, [this]
    {
        if (const auto lufs = loudnessPreset->currentData().toDouble(); lufs < 0.0)
            targetBox->setValue (lufs);
        readForm();
    });
    connect (browse, &QPushButton::clicked, this, [this]
    {
        const auto folder = QFileDialog::getExistingDirectory (this, tr ("Export To"), folderEdit->text().isEmpty()
                                                                                            ? QFileInfo (context.sourcePath).absolutePath()
                                                                                            : folderEdit->text());
        if (! folder.isEmpty())
        {
            folderEdit->setText (folder);
            readForm();
            refreshPath();
        }
    });
    connect (previewButton, &QPushButton::toggled, this, [this] (bool on)
    {
        readForm();
        if (on && shown >= 0)
            Q_EMIT previewRequested (list[(size_t) shown], differenceBox->isChecked());
        else
            Q_EMIT previewStopped();
    });
    connect (differenceBox, &QCheckBox::toggled, this, [this]
    {
        if (previewButton->isChecked() && shown >= 0)
            Q_EMIT previewRequested (list[(size_t) shown], differenceBox->isChecked());
    });
    connect (ab, &QPushButton::clicked, this, &ExportDialog::abRequested);
    connect (buttons, &QDialogButtonBox::accepted, this, [this]
    {
        readForm();
        if (context.forBatch)
        {
            accept();
            return;
        }
        remember (list);
        appSettings().setValue (QStringLiteral ("export/range"), rangeBox->currentData());
        if (masterBox->isEnabled())
            appSettings().setValue (QStringLiteral ("export/throughMaster"), masterBox->isChecked());
        accept();
    });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (this, &QDialog::finished, this, [this] { if (previewButton->isChecked()) Q_EMIT previewStopped(); });

    if (context.forBatch)
        for (int i = 0; i < bottom->count(); ++i)
            if (auto* w = bottom->itemAt (i)->widget())
                w->hide();

    refreshList();
    showOutput (0);
    outputList->setFocus();
}

ExportDialog::Range ExportDialog::range() const { return (Range) rangeBox->currentData().toInt(); }
bool ExportDialog::throughMaster() const        { return masterBox->isEnabled() && masterBox->isChecked(); }

void ExportDialog::refreshList()
{
    const QSignalBlocker block (outputList);
    const auto row = outputList->currentRow();
    outputList->clear();
    for (const auto& o : list)
    {
        const auto* f = codec::find (o.formatId);
        const auto format = f != nullptr ? qs (f->name) : qs (o.formatId);
        outputList->addItem (o.name.empty() || qs (o.name) == format ? format : tr ("%1  (%2)").arg (qs (o.name), format));
    }
    outputList->setCurrentRow (std::clamp (row, 0, (int) list.size() - 1));
}

void ExportDialog::buildSettings()
{
    for (; settingRows > 0; --settingRows)
        settingsForm->removeRow (settingsRow);
    if (shown < 0)
        return;
    auto& o = list[(size_t) shown];
    const auto* f = codec::find (o.formatId);
    if (f == nullptr)
        return;

    const auto updateVisibility = [this, f] ()
    {
        if (shown < 0)
            return;
        const auto& values = list[(size_t) shown].settings;
        for (int i = 0; i < (int) f->settings.size(); ++i)
            settingsForm->setRowVisible (settingsRow + i, codec::isShown (f->settings[(size_t) i], values));
    };

    for (const auto& s : f->settings)
    {
        auto value = o.settings.count (s.id) > 0 ? o.settings.at (s.id) : s.defaultValue;
        o.settings[s.id] = value;
        QWidget* editor = nullptr;
        if (s.kind == codec::Setting::Kind::choice)
        {
            auto* box = new QComboBox;
            for (const auto& [v, label] : s.choices)
                box->addItem (qs (label), qs (v));
            box->setCurrentIndex (std::max (0, box->findData (qs (value))));
            connect (box, &QComboBox::activated, this, [this, box, id = s.id, updateVisibility]
            {
                if (shown >= 0)
                    list[(size_t) shown].settings[id] = box->currentData().toString().toStdString();
                updateVisibility();
                refreshPath();
            });
            editor = box;
        }
        else if (s.kind == codec::Setting::Kind::number)
        {
            auto* spin = new QDoubleSpinBox;
            spin->setRange (s.minimum, s.maximum);
            spin->setSingleStep (s.step);
            spin->setDecimals (s.step < 1.0 ? 1 : 0);
            spin->setSuffix (qs (s.suffix));
            spin->setValue (std::atof (value.c_str()));
            connect (spin, &QDoubleSpinBox::valueChanged, this, [this, id = s.id] (double v)
            {
                if (shown >= 0)
                    list[(size_t) shown].settings[id] = QString::number (v).toStdString();
                refreshPath();
            });
            editor = spin;
        }
        else
        {
            auto* check = new QCheckBox;
            check->setChecked (value == "1");
            connect (check, &QCheckBox::toggled, this, [this, id = s.id] (bool on)
            {
                if (shown >= 0)
                    list[(size_t) shown].settings[id] = on ? "1" : "0";
            });
            editor = check;
        }
        settingsForm->insertRow (settingsRow + settingRows++, qs (s.label) + QLatin1Char (':'), editor);
    }
    updateVisibility();
}

void ExportDialog::showOutput (int row)
{
    if (row < 0 || row >= (int) list.size())
        return;
    shown = row;
    filling = true;
    const auto& o = list[(size_t) row];
    const auto* f = codec::find (o.formatId);

    nameEdit->setText (qs (o.name));
    formatBox->setCurrentIndex (std::max (0, formatBox->findData (qs (o.formatId))));
    buildSettings();

    rateBox->clear();
    rateBox->addItem (tr ("As the source (%1 Hz)").arg ((int) context.sampleRate), 0);
    for (const auto r : f->sampleRates.empty() ? std::vector<int> { 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 } : f->sampleRates)
        rateBox->addItem (tr ("%1 Hz").arg (r), r);
    rateBox->setCurrentIndex (std::max (0, rateBox->findData (o.sampleRate)));
    if (const auto actual = codec::rateFor (*f, o.sampleRate > 0 ? o.sampleRate : context.sampleRate); o.sampleRate == 0 && actual != (int) context.sampleRate)
        rateBox->setItemText (0, tr ("As the source: %1 Hz becomes %2 Hz").arg ((int) context.sampleRate).arg (actual));

    depthBox->clear();
    for (const auto b : f->bitDepths)
        depthBox->addItem (depthName (b), b);
    depthBox->setCurrentIndex (std::max (0, depthBox->findData (o.bits)));
    depthBox->setEnabled (! f->bitDepths.empty());
    if (f->bitDepths.empty())
        depthBox->addItem (tr ("Set by the codec"));
    ditherBox->setCurrentIndex (std::max (0, ditherBox->findData ((int) o.dither)));
    ditherBox->setEnabled (! f->bitDepths.empty() && o.bits <= 24);

    normalizeBox->setChecked (o.normalize);
    targetBox->setValue (o.targetLufs);
    targetBox->setEnabled (o.normalize);
    loudnessPreset->setEnabled (o.normalize);
    int preset = loudnessPreset->count() - 1;
    for (int i = 0; i < loudnessPreset->count() - 1; ++i)
        if (std::abs (loudnessPreset->itemData (i).toDouble() - o.targetLufs) < 0.05)
            preset = i;
    loudnessPreset->setCurrentIndex (preset);
    limitBox->setChecked (o.limit);
    ceilingBox->setValue (o.ceilingDb);
    ceilingBox->setEnabled (o.limit);

    folderEdit->setText (qs (o.folder));
    patternEdit->setText (qs (o.pattern));
    overwriteBox->setCurrentIndex (std::max (0, overwriteBox->findData ((int) o.overwrite)));

    templateBox->clear();
    templateBox->addItem (tr ("None: the file's own tags"), QString());
    for (const auto& name : MetadataEditor::templateNames())
        templateBox->addItem (name, MetadataEditor::templateText (name));
    const auto current = templateBox->findData (qs (o.templateText));
    if (current < 0 && ! o.templateText.empty())
        templateBox->addItem (tr ("(as saved with this output)"), qs (o.templateText));
    templateBox->setCurrentIndex (std::max (0, templateBox->findData (qs (o.templateText))));

    // Which schemas, for this format.
    auto* schemaLayout = static_cast<QVBoxLayout*> (schemaArea->layout());
    while (auto* item = schemaLayout->takeAt (0))
    {
        delete item->widget();
        delete item;
    }
    const auto ext = f->extension;
    const auto schemas = AudioEngine::schemasOf (ext);
    const auto has = [&] (meta::Schema s) { return std::find (schemas.begin(), schemas.end(), s) != schemas.end(); };
    const auto addBox = [this, schemaLayout] (const QString& label, bool tagio::Options::* flag)
    {
        auto* check = new QCheckBox (label);
        check->setChecked (list[(size_t) shown].schemas.*flag);
        connect (check, &QCheckBox::toggled, this, [this, flag] (bool on) { if (shown >= 0) list[(size_t) shown].schemas.*flag = on; });
        schemaLayout->addWidget (check);
    };
    if (f->native || f->taggedByTagLib)
    {
        if (ext == "wav")
        {
            addBox (tr ("RIFF INFO"), &tagio::Options::riff);
            addBox (tr ("Broadcast Wave (bext)"), &tagio::Options::bext);
            addBox (tr ("Cart chunk"), &tagio::Options::cart);
            addBox (tr ("iXML"), &tagio::Options::ixml);
        }
        if (ext == "aiff")
            addBox (tr ("AIFF text chunks"), &tagio::Options::riff);
        if (has (meta::Schema::id3))
            addBox (tr ("ID3v2"), &tagio::Options::id3);
        if (ext == "mp3")
            addBox (tr ("ID3v1"), &tagio::Options::id3v1);
        if (has (meta::Schema::vorbis))
            addBox (tr ("Vorbis comments and pictures"), &tagio::Options::vorbis);
        if (has (meta::Schema::mp4))
            addBox (tr ("MP4 / iTunes atoms"), &tagio::Options::mp4);
        if (has (meta::Schema::ape))
            addBox (tr ("APEv2"), &tagio::Options::ape);
        if (has (meta::Schema::xmp))
            addBox (tr ("XMP"), &tagio::Options::xmp);
    }
    else
        schemaLayout->addWidget (new QLabel (tr ("Title, artist, album and the other common tags, as FFmpeg writes them for this container")));
    if (f->chapters)
        addBox (ext == "wav" ? tr ("Markers as cue points and regions") : tr ("Markers as chapters"), &tagio::Options::chapters);

    fillLoudnessBox->setChecked (o.fillLoudness);
    fillLoudnessBox->setVisible (ext == "wav");
    checkPeaksBox->setChecked (o.checkPeaks);
    checkPeaksBox->setVisible (! f->lossless);
    noteLabel->setText (qs (f->note));
    filling = false;
    refreshPath();
}

void ExportDialog::readForm()
{
    if (filling || shown < 0 || shown >= (int) list.size())
        return;
    auto& o = list[(size_t) shown];
    o.name = nameEdit->text().toStdString();
    o.sampleRate = rateBox->currentData().toInt();
    if (depthBox->isEnabled())
        o.bits = depthBox->currentData().toInt();
    o.dither = (dsp::Dither) ditherBox->currentData().toInt();
    o.normalize = normalizeBox->isChecked();
    o.targetLufs = targetBox->value();
    o.limit = limitBox->isChecked();
    o.ceilingDb = ceilingBox->value();
    o.folder = folderEdit->text().trimmed().toStdString();
    o.pattern = patternEdit->text().toStdString();
    o.overwrite = (exporter::Output::Overwrite) overwriteBox->currentData().toInt();
    o.templateText = templateBox->currentData().toString().toStdString();
    o.fillLoudness = fillLoudnessBox->isChecked();
    o.checkPeaks = checkPeaksBox->isChecked();
}

void ExportDialog::refreshPath()
{
    if (shown < 0)
        return;
    exporter::Source s;
    s.sourcePath = context.sourcePath.toStdString();
    s.tags = context.tags;
    s.part = range() == Range::regions || range() == Range::markers ? tr ("Part name").toStdString() : std::string();
    s.index = 1;
    AudioData::Builder empty (context.channels, context.sampleRate);
    s.data = empty.finish();
    pathLabel->setText (tr ("Writes %1").arg (qs (exporter::targetPath (s, list[(size_t) shown]))));
}

void ExportDialog::loadPreset()
{
    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("export/presets"));
    QStringList names;
    for (const auto& key : settings.childKeys())
        names << QUrl::fromPercentEncoding (key.toUtf8());
    settings.endGroup();
    // Presets that come with PHINYX.
    const QStringList builtIn { tr ("Streaming: WAV master, MP3 320 and AAC 256 at -14 LUFS"), tr ("CD: 16-bit 44.1 kHz WAV"),
                                tr ("Podcast: MP3 128 at -16 LUFS"), tr ("Broadcast: 24-bit 48 kHz BWF at -23 LUFS") };
    bool ok = false;
    const auto choice = QInputDialog::getItem (this, tr ("Load a Preset"), tr ("Preset:"), builtIn + names, 0, false, &ok);
    if (! ok)
        return;

    std::vector<exporter::Output> loaded;
    const auto make = [] (const char* format, const QString& name, double lufs, int rate, int bits)
    {
        auto o = defaultOutput (format);
        o.name = name.toStdString();
        o.normalize = lufs < 0.0;
        o.targetLufs = lufs < 0.0 ? lufs : -14.0;
        o.limit = lufs < 0.0;
        o.ceilingDb = -1.0;
        o.sampleRate = rate;
        if (bits > 0)
            o.bits = bits;
        o.pattern = std::string ("{filename}") + (std::string (format) == "wav" ? "" : " ({format})");
        return o;
    };
    if (choice == builtIn[0])
    {
        loaded.push_back (make ("wav", tr ("Master WAV"), 0.0, 0, 24));
        loaded.push_back (make ("mp3", tr ("MP3 320"), -14.0, 0, 0));
        loaded.push_back (make ("aac-m4a", tr ("AAC 256"), -14.0, 0, 0));
    }
    else if (choice == builtIn[1])
        loaded.push_back (make ("wav", tr ("CD WAV"), 0.0, 44100, 16));
    else if (choice == builtIn[2])
    {
        auto o = make ("mp3", tr ("Podcast MP3"), -16.0, 44100, 0);
        o.settings["bitrate"] = "128";
        loaded.push_back (o);
    }
    else if (choice == builtIn[3])
    {
        auto o = make ("wav", tr ("Broadcast WAV"), -23.0, 48000, 24);
        o.ceilingDb = -1.0;
        loaded.push_back (o);
    }
    else
        loaded = exporter::fromJson (appSettings().value (QStringLiteral ("export/presets/") + QString::fromUtf8 (QUrl::toPercentEncoding (choice))).toString().toStdString());
    std::erase_if (loaded, [] (const auto& o) { return codec::find (o.formatId) == nullptr; });
    if (loaded.empty())
        return;
    for (auto& o : loaded)
        o.schemas.software = APP_DISPLAY_NAME;
    list = loaded;
    shown = -1;
    refreshList();
    outputList->setCurrentRow (0);
    showOutput (0);
}

void ExportDialog::savePreset()
{
    readForm();
    bool ok = false;
    const auto name = QInputDialog::getText (this, tr ("Save as a Preset"), tr ("Preset name:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (! ok || name.isEmpty())
        return;
    appSettings().setValue (QStringLiteral ("export/presets/") + QString::fromUtf8 (QUrl::toPercentEncoding (name)), qs (exporter::toJson (list)));
}
