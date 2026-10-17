// SPDX-License-Identifier: AGPL-3.0-only
#include "Dialogs.h"
#include "../core/Repair.h"
#include "CommandRegistry.h"
#include "DesktopShortcuts.h"
#include "Theme.h"
#include "../engine/AudioEngine.h"

#include <QCheckBox>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>
#include <QTabWidget>
#include <QStandardPaths>
#include <QSettings>
#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QPainter>
#include <QPixmap>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QListWidget>
#include <QUrl>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

//==============================================================================
CommandPalette::CommandPalette (CommandRegistry& cm, QWidget* parent)
    : QDialog (parent), commands (cm), search (new QLineEdit (this)), list (new QTreeWidget (this))
{
    setWindowTitle (tr ("Command Palette"));
    resize (640, 440);

    search->setPlaceholderText (tr ("Type to search commands and shortcuts..."));
    search->setAccessibleName (tr ("Search commands"));
    search->installEventFilter (this);

    list->setColumnCount (3);
    list->setHeaderLabels ({ tr ("Command"), tr ("Category"), tr ("Shortcut") });
    list->setRootIsDecorated (false);
    list->setAllColumnsShowFocus (true);
    list->header()->setSectionResizeMode (0, QHeaderView::Stretch);
    list->setAccessibleName (tr ("Commands"));

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (search);
    layout->addWidget (list);

    connect (search, &QLineEdit::textChanged, this, &CommandPalette::refilter);
    connect (list, &QTreeWidget::itemActivated, this, &CommandPalette::runSelected);

    refilter();
    search->setFocus();
}

void CommandPalette::refilter()
{
    const auto tokens = search->text().split (QLatin1Char (' '), Qt::SkipEmptyParts);
    list->clear();

    for (const auto& c : commands.all())
    {
        const auto haystack = c.category + QLatin1Char (' ') + c.name() + QLatin1Char (' ') + c.shortcutText();
        const bool matches = std::all_of (tokens.begin(), tokens.end(),
                                          [&] (const QString& t) { return haystack.contains (t, Qt::CaseInsensitive); });

        if (! matches)
            continue;

        auto* item = new QTreeWidgetItem (list, { c.name(), c.category, c.shortcutText() });
        item->setData (0, Qt::UserRole, c.id);
        item->setDisabled (! c.action->isEnabled());
    }

    if (list->topLevelItemCount() > 0)
        list->setCurrentItem (list->topLevelItem (0));
}

bool CommandPalette::eventFilter (QObject* obj, QEvent* event)
{
    if (obj == search && event->type() == QEvent::KeyPress)
    {
        const auto key = static_cast<QKeyEvent*> (event)->key();

        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown)
        {
            QCoreApplication::sendEvent (list, event);
            return true;
        }

        if (key == Qt::Key_Return || key == Qt::Key_Enter)
        {
            runSelected();
            return true;
        }
    }

    return QDialog::eventFilter (obj, event);
}

void CommandPalette::runSelected()
{
    auto* item = list->currentItem();
    if (item == nullptr || item->isDisabled())
        return;

    auto* action = commands.action (item->data (0, Qt::UserRole).toString());
    accept();

    // Run after the palette has closed so focus is back on the main window.
    QTimer::singleShot (0, action, &QAction::trigger);
}

//==============================================================================
KeymapDialog::KeymapDialog (CommandRegistry& cm, QWidget* parent)
    : QDialog (parent), commands (cm), tree (new QTreeWidget (this))
{
    setWindowTitle (tr ("Keyboard Shortcuts"));
    resize (620, 640);

    tree->setColumnCount (3);
    tree->setHeaderLabels ({ tr ("Command"), tr ("Shortcuts"), tr ("Note") });
    tree->header()->setSectionResizeMode (0, QHeaderView::Stretch);
    tree->setAccessibleName (tr ("Commands and their shortcuts"));

    auto* edit  = new QPushButton (tr ("&Change..."), this);
    auto* clear = new QPushButton (tr ("C&lear"), this);
    auto* reset = new QPushButton (tr ("&Reset All to Defaults"), this);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, this);
    buttons->addButton (edit, QDialogButtonBox::ActionRole);
    buttons->addButton (clear, QDialogButtonBox::ActionRole);
    buttons->addButton (reset, QDialogButtonBox::ResetRole);

    // Enter in the list must edit the selected shortcut, never trigger a button.
    for (auto* b : buttons->buttons())
        if (auto* pb = qobject_cast<QPushButton*> (b))
            pb->setAutoDefault (false);

    auto* hint = new QLabel (tr ("Select a command and press Enter to change its shortcut. Changes are saved automatically.")
                             + (DesktopShortcuts::taken().isEmpty() ? QString()
                                : tr (" Shortcuts your desktop (Hyprland) already uses are marked in the Note column.")), this);
    hint->setWordWrap (true);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (hint);
    layout->addWidget (tree);
    layout->addWidget (buttons);

    connect (tree, &QTreeWidget::itemActivated, this, &KeymapDialog::editSelected);
    connect (edit, &QPushButton::clicked, this, &KeymapDialog::editSelected);
    connect (clear, &QPushButton::clicked, this, &KeymapDialog::clearSelected);
    connect (reset, &QPushButton::clicked, this, [this]
    {
        commands.resetToDefaults();
        commands.save();
        rebuild();
    });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    rebuild();
    QTimer::singleShot (0, tree, [this] { tree->setFocus(); });
}

void KeymapDialog::rebuild()
{
    const auto currentId = tree->currentItem() ? tree->currentItem()->data (0, Qt::UserRole).toString() : QString();
    tree->clear();

    QMap<QString, QTreeWidgetItem*> categories;

    for (const auto& c : commands.all())
    {
        auto*& parent = categories[c.category];
        if (parent == nullptr)
        {
            parent = new QTreeWidgetItem (tree, { c.category });
            parent->setFlags (Qt::ItemIsEnabled);
            parent->setExpanded (true);
        }

        // Keys the desktop (Hyprland) keeps for itself never reach PHINYX.
        QStringList notes;
        for (const auto& key : c.action->shortcuts())
            if (const auto owner = DesktopShortcuts::owner (key); ! owner.isEmpty())
                notes << tr ("%1 is taken by %2").arg (key.toString (QKeySequence::NativeText), owner);

        auto* item = new QTreeWidgetItem (parent, { c.name(), c.shortcutText(), notes.join (QStringLiteral ("; ")) });
        item->setData (0, Qt::UserRole, c.id);
        if (! notes.isEmpty())
            item->setForeground (2, palette().color (QPalette::Link));

        if (c.id == currentId || tree->currentItem() == nullptr)
            tree->setCurrentItem (item);
    }
}

void KeymapDialog::editSelected()
{
    auto* item = tree->currentItem();
    if (item == nullptr || item->data (0, Qt::UserRole).isNull())
        return;

    const auto id = item->data (0, Qt::UserRole).toString();

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Change Shortcut: %1").arg (item->text (0)));
    auto* editor = new QKeySequenceEdit (&dialog);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* layout = new QVBoxLayout (&dialog);
    layout->addWidget (new QLabel (tr ("Press the new key combination, then Enter. Esc cancels."), &dialog));
    layout->addWidget (editor);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect (editor, &QKeySequenceEdit::editingFinished, &dialog, &QDialog::accept);
    editor->setFocus();

    if (dialog.exec() != QDialog::Accepted || editor->keySequence().isEmpty())
        return;

    // Only the first chord is used; multi-chord sequences are a later feature.
    const QKeySequence seq (editor->keySequence()[0]);

    if (const auto owner = DesktopShortcuts::owner (seq); ! owner.isEmpty())
    {
        if (QMessageBox::question (this, tr ("Taken by the desktop"),
                tr ("%1 is used by your desktop (%2), so PHINYX would never receive it. Use it anyway?")
                    .arg (seq.toString (QKeySequence::NativeText), owner))
            != QMessageBox::Yes)
            return;
    }

    if (const auto* other = commands.findConflict (seq, id))
    {
        const auto answer = QMessageBox::question (this, tr ("Shortcut in use"),
            tr ("%1 is already used by \"%2\". Move it to \"%3\"?")
                .arg (seq.toString (QKeySequence::NativeText), other->name(), item->text (0)));

        if (answer != QMessageBox::Yes)
            return;

        auto keys = other->action->shortcuts();
        keys.removeAll (seq);
        commands.setShortcuts (other->id, keys);
    }

    commands.setShortcuts (id, { seq });
    commands.save();
    rebuild();
}

void KeymapDialog::clearSelected()
{
    auto* item = tree->currentItem();
    if (item == nullptr || item->data (0, Qt::UserRole).isNull())
        return;

    commands.setShortcuts (item->data (0, Qt::UserRole).toString(), {});
    commands.save();
    rebuild();
}

//==============================================================================
AudioSettingsDialog::AudioSettingsDialog (AudioEngine& e, QWidget* parent)
    : QDialog (parent), engine (e), outputs (new QComboBox (this)), status (new QLabel (this))
{
    setWindowTitle (tr ("Audio Settings"));
    resize (520, 160);

    const auto current = engine.currentOutput();

    for (const auto& choice : engine.availableOutputs())
    {
        outputs->addItem (QString::fromStdString (choice.type + ": " + choice.name),
                          QStringList { QString::fromStdString (choice.type), QString::fromStdString (choice.name) });

        if (choice.type == current.type && choice.name == current.name)
            outputs->setCurrentIndex (outputs->count() - 1);
    }

    auto* label = new QLabel (tr ("&Output device:"), this);
    label->setBuddy (outputs);
    status->setText (QString::fromStdString (engine.deviceDescription()));
    status->setWordWrap (true);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (label);
    layout->addWidget (outputs);
    layout->addWidget (status);
    layout->addWidget (buttons);

    connect (buttons->button (QDialogButtonBox::Apply), &QPushButton::clicked, this, &AudioSettingsDialog::apply);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    outputs->setFocus();
}

void AudioSettingsDialog::apply()
{
    const auto choice = outputs->currentData().toStringList();
    if (choice.size() != 2)
        return;

    const auto error = engine.selectOutput ({ choice[0].toStdString(), choice[1].toStdString() });
    status->setText (error.empty() ? QString::fromStdString (engine.deviceDescription())
                                   : tr ("Could not open device: %1").arg (QString::fromStdString (error)));
}

//==============================================================================
SaveOptionsDialog::SaveOptionsDialog (const QString& extension, SaveOptions& o, QWidget* parent)
    : QDialog (parent), options (o)
{
    setWindowTitle (tr ("Save Options"));
    const auto ext = extension.toLower();
    auto* form = new QFormLayout;

    if (ext == QLatin1String ("ogg"))
    {
        quality = new QSpinBox (this);
        quality->setRange (0, 10);
        quality->setValue (options.oggQuality);
        form->addRow (tr ("&Quality (0-10):"), quality);
    }
    else
    {
        bitDepth = new QComboBox (this);
        bitDepth->addItem (tr ("16-bit"), 16);
        bitDepth->addItem (tr ("24-bit"), 24);
        if (ext != QLatin1String ("flac"))
            bitDepth->addItem (tr ("32-bit float"), 32);

        bitDepth->setCurrentIndex (std::max (0, bitDepth->findData (options.bitDepth)));
        form->addRow (tr ("&Bit depth:"), bitDepth);

        dither = new QCheckBox (tr ("Add &dither when reducing bit depth"), this);
        dither->setChecked (options.dither);
        form->addRow (dither);
    }

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addLayout (form);
    layout->addWidget (buttons);

    connect (buttons, &QDialogButtonBox::accepted, this, [this] { store(); accept(); });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SaveOptionsDialog::store()
{
    if (bitDepth != nullptr)
        options.bitDepth = bitDepth->currentData().toInt();
    if (dither != nullptr)
        options.dither = dither->isChecked();
    if (quality != nullptr)
        options.oggQuality = quality->value();
}

//==============================================================================
NormalizeDialog::NormalizeDialog (QWidget* parent)
    : QDialog (parent), modeBox (new QComboBox (this)), targetBox (new QDoubleSpinBox (this))
{
    setWindowTitle (tr ("Normalize"));
    modeBox->addItem (tr ("Peak level (dBFS)"));
    modeBox->addItem (tr ("Integrated loudness (LUFS)"));
    targetBox->setDecimals (1);
    targetBox->setSingleStep (0.5);

    auto applyMode = [this] (int index)
    {
        if (index == 0)
        {
            targetBox->setRange (-60.0, 0.0);
            targetBox->setValue (-1.0);
        }
        else
        {
            targetBox->setRange (-60.0, 0.0);
            targetBox->setValue (-14.0);
        }
    };

    applyMode (0);
    connect (modeBox, &QComboBox::currentIndexChanged, this, applyMode);

    auto* form = new QFormLayout;
    form->addRow (tr ("&Normalize to:"), modeBox);
    form->addRow (tr ("&Target:"), targetBox);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addLayout (form);
    layout->addWidget (buttons);

    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    modeBox->setFocus();
}

StereoizeDialog::StereoizeDialog (const dsp::StereoizeSettings& initial, bool wholeFile, int fileChannels, QWidget* parent)
    : QDialog (parent), widthBox (new QSpinBox (this)), delayBox (new QDoubleSpinBox (this)), bassBox (new QSpinBox (this))
{
    setWindowTitle (tr ("Stereoize"));

    widthBox->setRange (0, 100);
    widthBox->setSuffix (tr (" %"));
    widthBox->setValue ((int) std::lround (initial.width * 100.0));
    widthBox->setToolTip (tr ("How wide the result sounds. 0% leaves it mono."));

    delayBox->setRange (1.0, 40.0);
    delayBox->setDecimals (1);
    delayBox->setSingleStep (1.0);
    delayBox->setSuffix (tr (" ms"));
    delayBox->setValue (initial.delayMs);
    delayBox->setToolTip (tr ("Shorter sounds brighter and tighter, longer sounds roomier"));

    bassBox->setRange (0, 1000);
    bassBox->setSingleStep (10);
    bassBox->setSuffix (tr (" Hz"));
    bassBox->setSpecialValueText (tr ("Off (widen everything)"));
    bassBox->setValue ((int) std::lround (initial.bassHz));
    bassBox->setToolTip (tr ("Bass below this stays in the centre, which keeps the low end solid"));

    auto* about = new QLabel (this);
    about->setWordWrap (true);
    const auto what = fileChannels == 1 ? tr ("Turns this mono file into stereo.") : tr ("Rebuilds the stereo image from the middle of the sound.");
    const auto where = wholeFile ? tr ("It applies to the whole file.") : tr ("It applies to the selection; a mono file becomes stereo, the same on both sides outside it.");
    about->setText (what + QLatin1Char (' ') + where + QLatin1Char (' ')
                    + tr ("Played back in mono it sounds exactly like the original."));

    auto* form = new QFormLayout;
    form->addRow (tr ("&Width:"), widthBox);
    form->addRow (tr ("&Delay:"), delayBox);
    form->addRow (tr ("Keep &bass centred below:"), bassBox);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (form);
    layout->addWidget (buttons);

    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    widthBox->setFocus();
    widthBox->selectAll();
}

dsp::StereoizeSettings StereoizeDialog::settings() const
{
    return { widthBox->value() / 100.0, delayBox->value(), (double) bassBox->value() };
}

//==============================================================================
FrequencyRangeDialog::FrequencyRangeDialog (double lowHz, double highHz, double nyquist, QWidget* parent)
    : QDialog (parent), lowBox (new QSpinBox (this)), highBox (new QSpinBox (this))
{
    setWindowTitle (tr ("Frequency Range"));
    const auto top = (int) std::floor (nyquist);
    for (auto* box : { lowBox, highBox })
    {
        box->setRange (0, top);
        box->setSingleStep (50);
        box->setSuffix (tr (" Hz"));
        box->setAccelerated (true);
    }
    lowBox->setValue ((int) std::lround (lowHz));
    highBox->setValue (highHz > lowHz ? (int) std::lround (highHz) : top);

    auto* about = new QLabel (tr ("Spectral edits, Auto Heal and Delete then work only on these frequencies "
                                  "inside the time selection. From 0 Hz to %1 Hz means every frequency.").arg (top), this);
    about->setWordWrap (true);

    auto* form = new QFormLayout;
    form->addRow (tr ("&From:"), lowBox);
    form->addRow (tr ("&To:"), highBox);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (form);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lowBox->setFocus();
    lowBox->selectAll();
}

double FrequencyRangeDialog::lowHz() const  { return std::min (lowBox->value(), highBox->value()); }
double FrequencyRangeDialog::highHz() const
{
    // The full range is stored as "no band".
    const auto lo = std::min (lowBox->value(), highBox->value()), hi = std::max (lowBox->value(), highBox->value());
    return lo == 0 && hi == highBox->maximum() ? 0.0 : hi;
}

SpectralGainDialog::SpectralGainDialog (double initialDb, const QString& what, QWidget* parent)
    : QDialog (parent), gainBox (new QDoubleSpinBox (this))
{
    setWindowTitle (tr ("Spectral Gain"));
    gainBox->setRange (-60.0, 20.0);
    gainBox->setDecimals (1);
    gainBox->setSingleStep (1.0);
    gainBox->setSuffix (tr (" dB"));
    gainBox->setSpecialValueText (tr ("Remove"));
    gainBox->setValue (initialDb);

    auto* about = new QLabel (tr ("Turns %1 down or up, with soft edges so nothing clicks. "
                                  "Below 0 dB is quieter; the lowest setting removes it.").arg (what), this);
    about->setWordWrap (true);

    auto* form = new QFormLayout;
    form->addRow (tr ("&Gain:"), gainBox);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (form);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    gainBox->setFocus();
    gainBox->selectAll();
}

double SpectralGainDialog::gainDb() const
{
    return gainBox->value() <= gainBox->minimum() ? repair::removeDb : gainBox->value();
}

//==============================================================================
ValuesDialog::ValuesDialog (const QString& title, const QString& about, QList<Field> f, QWidget* parent)
    : QDialog (parent), fields (std::move (f))
{
    setWindowTitle (title);
    QSettings settings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);

    auto* text = new QLabel (about, this);
    text->setWordWrap (true);
    auto* form = new QFormLayout;
    for (const auto& field : fields)
    {
        const auto value = settings.value (field.key, field.defaultValue).toDouble();
        QWidget* editor = nullptr;
        if (! field.choices.isEmpty())
        {
            auto* box = new QComboBox (this);
            box->addItems (field.choices);
            box->setCurrentIndex (std::clamp ((int) value, 0, (int) field.choices.size() - 1));
            editor = box;
        }
        else
        {
            auto* box = new QDoubleSpinBox (this);
            box->setRange (field.minimum, field.maximum);
            box->setSingleStep (field.step);
            box->setDecimals (field.decimals);
            box->setSuffix (field.suffix);
            box->setValue (std::clamp (value, field.minimum, field.maximum));
            box->setAccelerated (true);
            editor = box;
        }
        editor->setToolTip (field.tip);
        form->addRow (field.label, editor);
        editors.push_back (editor);
    }

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (text);
    layout->addLayout (form);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (! editors.isEmpty())
    {
        editors.front()->setFocus();
        if (auto* spin = qobject_cast<QDoubleSpinBox*> (editors.front()))
            spin->selectAll();
    }
}

double ValuesDialog::value (int index) const
{
    auto* editor = editors.value (index);
    if (auto* box = qobject_cast<QComboBox*> (editor))
        return box->currentIndex();
    if (auto* spin = qobject_cast<QDoubleSpinBox*> (editor))
        return spin->value();
    return 0.0;
}

void ValuesDialog::accept()
{
    QSettings settings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
    for (int i = 0; i < fields.size(); ++i)
        settings.setValue (fields[i].key, value (i));
    QDialog::accept();
}

NormalizeDialog::Mode NormalizeDialog::mode() const { return modeBox->currentIndex() == 0 ? Mode::peak : Mode::loudness; }
double NormalizeDialog::target() const              { return targetBox->value(); }

//==============================================================================
WaveformColorsDialog::WaveformColorsDialog (QWidget* parent)
    : QDialog (parent), original (Theme::colorOverrides())
{
    setWindowTitle (tr ("Waveform Colors"));

    const QList<QPair<QString, QString>> rows {
        { QStringLiteral ("waveform"),           tr ("&Waveform") },
        { QStringLiteral ("waveformBackground"), tr ("&Background") },
        { QStringLiteral ("selection"),          tr ("&Selection") },
        { QStringLiteral ("cursor"),             tr ("&Cursor") },
        { QStringLiteral ("centerLine"),         tr ("Center &line") },
        { QStringLiteral ("marker"),             tr ("&Markers") },
        { QStringLiteral ("region"),             tr ("&Regions") },
    };

    auto* grid = new QGridLayout;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto key = rows[i].first;
        const auto name = rows[i].second;

        auto* label = new QLabel (name, this);
        auto* swatch = new QPushButton (this);
        swatch->setAutoDefault (false);
        swatch->setMinimumWidth (170);
        label->setBuddy (swatch);
        auto* reset = new QPushButton (tr ("Theme default"), this);
        reset->setAutoDefault (false);
        reset->setAccessibleName (tr ("Reset %1 to the theme's colour").arg (QString (name).remove (QLatin1Char ('&'))));

        grid->addWidget (label, i, 0);
        grid->addWidget (swatch, i, 1);
        grid->addWidget (reset, i, 2);
        swatches.push_back ({ key, swatch });

        connect (swatch, &QPushButton::clicked, this, [this, key, name]
        {
            const auto chosen = QColorDialog::getColor (Theme::current().color (key), this,
                                                        QString (name).remove (QLatin1Char ('&')),
                                                        QColorDialog::ShowAlphaChannel);
            if (chosen.isValid())
            {
                Theme::setColorOverride (key, chosen);
                refresh();
            }
        });
        connect (reset, &QPushButton::clicked, this, [this, key]
        {
            Theme::setColorOverride (key, {});
            refresh();
        });
    }

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* resetAll = buttons->addButton (tr ("Reset &All"), QDialogButtonBox::ResetRole);
    for (auto* b : buttons->buttons())
        if (auto* push = qobject_cast<QPushButton*> (b))
            push->setAutoDefault (false);

    auto* note = new QLabel (tr ("Press Enter or Space on a colour to change it. These colours apply on top of any theme."), this);
    note->setWordWrap (true);

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (grid);
    layout->addWidget (note);
    layout->addWidget (buttons);

    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (resetAll, &QPushButton::clicked, this, [this]
    {
        auto overrides = Theme::colorOverrides();
        for (const auto& s : swatches)
            overrides.remove (s.first);
        Theme::setColorOverrides (overrides);
        refresh();
    });

    refresh();
    swatches.first().second->setFocus();
}

void WaveformColorsDialog::refresh()
{
    const auto overrides = Theme::colorOverrides();
    for (const auto& [key, swatch] : swatches)
    {
        const auto c = Theme::current().color (key);
        QPixmap chip (28, 14);
        chip.fill (Qt::transparent);
        QPainter g (&chip);
        g.fillRect (chip.rect(), Qt::black);
        g.fillRect (chip.rect(), c);
        g.end();
        swatch->setIcon (chip);
        swatch->setIconSize (chip.size());
        swatch->setText (Theme::formatColor (c) + (overrides.contains (key) ? QString() : tr (" (theme)")));
    }
}

void WaveformColorsDialog::done (int result)
{
    if (result != QDialog::Accepted)
        Theme::setColorOverrides (original);

    QDialog::done (result);
}

//==============================================================================
ThemeDialog::ThemeDialog (QWidget* parent)
    : QDialog (parent), list (new QListWidget (this)), info (new QLabel (this)), originalName (Theme::current().name)
{
    setWindowTitle (tr ("Theme"));
    resize (460, 360);
    list->setAccessibleName (tr ("Themes"));
    info->setWordWrap (true);
    info->setTextInteractionFlags (Qt::TextSelectableByMouse);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* folder = buttons->addButton (tr ("Open Themes &Folder"), QDialogButtonBox::ActionRole);
    auto* refresh = buttons->addButton (tr ("Re&load"), QDialogButtonBox::ActionRole);
    for (auto* b : buttons->buttons())
        if (auto* push = qobject_cast<QPushButton*> (b))
            push->setAutoDefault (false);

    auto* label = new QLabel (tr ("&Themes (add your own as JSON files in the themes folder):"), this);
    label->setBuddy (list);
    label->setWordWrap (true);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (label);
    layout->addWidget (list, 1);
    layout->addWidget (info);
    layout->addWidget (buttons);

    connect (list, &QListWidget::currentRowChanged, this, &ThemeDialog::preview);
    connect (list, &QListWidget::itemActivated, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (refresh, &QPushButton::clicked, this, &ThemeDialog::reload);
    connect (folder, &QPushButton::clicked, this, []
    {
        QDir().mkpath (Theme::userThemeFolder());
        QDesktopServices::openUrl (QUrl::fromLocalFile (Theme::userThemeFolder()));
    });

    reload();
    list->setFocus();
}

void ThemeDialog::reload()
{
    QStringList errors;
    const auto themes = Theme::available (&errors);
    const auto selected = list->currentItem() != nullptr ? list->currentItem()->text() : Theme::current().name;

    const QSignalBlocker blocker (list);
    list->clear();
    for (const auto& t : themes)
    {
        list->addItem (t.name);
        if (t.name == selected)
            list->setCurrentRow (list->count() - 1);
    }

    if (list->currentRow() < 0)
        list->setCurrentRow (0);

    auto text = tr ("Themes folder: %1").arg (Theme::userThemeFolder());
    if (! errors.isEmpty())
        text += QStringLiteral ("\n") + tr ("Skipped: %1").arg (errors.join (QStringLiteral ("; ")));
    info->setText (text);
}

void ThemeDialog::preview()
{
    if (list->currentItem() == nullptr)
        return;

    for (const auto& t : Theme::available())
        if (t.name == list->currentItem()->text())
        {
            Theme::apply (t);
            return;
        }
}

void ThemeDialog::done (int result)
{
    if (result == QDialog::Accepted)
        Theme::saveName (Theme::current().name);
    else
        for (const auto& t : Theme::available())
            if (t.name == originalName)
                Theme::apply (t);

    QDialog::done (result);
}

//==============================================================================
MetadataDialog::MetadataDialog (const Tags& current, const QString& extension, double sampleRate, QWidget* parent)
    : QDialog (parent), original (current)
{
    setWindowTitle (tr ("Metadata"));
    resize (560, 480);

    auto add = [this, &current] (QFormLayout* form, const char* key, const QString& label, bool multiLine, int maxLength)
    {
        const auto value = current.count (key) ? QString::fromStdString (current.at (key)) : QString();
        QWidget* field;
        if (multiLine)
        {
            auto* edit = new QPlainTextEdit (value);
            edit->setTabChangesFocus (true);
            edit->setFixedHeight (edit->fontMetrics().lineSpacing() * 4 + 12);
            field = edit;
        }
        else
        {
            auto* edit = new QLineEdit (value);
            if (maxLength > 0)
                edit->setMaxLength (maxLength);
            field = edit;
        }
        form->addRow (label, field);
        fields.push_back ({ key, field });
        return field;
    };

    auto* tabs = new QTabWidget (this);
    auto* common = new QWidget;
    auto* commonForm = new QFormLayout (common);
    add (commonForm, tag::title, tr ("&Title:"), false, 0);
    add (commonForm, tag::artist, tr ("&Artist:"), false, 0);
    add (commonForm, tag::album, tr ("Al&bum:"), false, 0);
    add (commonForm, tag::track, tr ("Trac&k:"), false, 0);
    add (commonForm, tag::genre, tr ("&Genre:"), false, 0);
    add (commonForm, tag::date, tr ("&Date:"), false, 0);
    add (commonForm, tag::copyright, tr ("&Copyright:"), false, 0);
    add (commonForm, tag::isrc, tr ("&ISRC:"), false, 12);
    add (commonForm, tag::comment, tr ("Co&mment:"), true, 0);
    tabs->addTab (common, tr ("Tags"));

    auto* bwf = new QWidget;
    auto* bwfForm = new QFormLayout (bwf);
    add (bwfForm, tag::bwfDescription, tr ("&Description:"), false, 256);
    auto* originator = static_cast<QLineEdit*> (add (bwfForm, tag::bwfOriginator, tr ("&Originator:"), false, 32));
    auto* reference = static_cast<QLineEdit*> (add (bwfForm, tag::bwfOriginatorRef, tr ("Originator &reference:"), false, 32));
    auto* date = static_cast<QLineEdit*> (add (bwfForm, tag::bwfDate, tr ("Origination d&ate:"), false, 10));
    date->setPlaceholderText (tr ("YYYY-MM-DD"));
    auto* time = static_cast<QLineEdit*> (add (bwfForm, tag::bwfTime, tr ("Origination &time:"), false, 8));
    time->setPlaceholderText (tr ("HH:MM:SS"));
    auto* timeReference = static_cast<QLineEdit*> (add (bwfForm, tag::bwfTimeReference, tr ("Time r&eference:"), false, 20));
    timeReference->setValidator (new QRegularExpressionValidator (QRegularExpression (QStringLiteral ("[0-9]*")), timeReference));
    timeReference->setToolTip (tr ("Where the file starts on the session timeline, in samples since midnight"));
    auto* seconds = new QLabel;
    bwfForm->addRow (QString(), seconds);
    auto showSeconds = [seconds, timeReference, sampleRate]
    {
        const auto samples = timeReference->text().toLongLong();
        const auto total = sampleRate > 0.0 ? (double) samples / sampleRate : 0.0;
        seconds->setText (tr ("= %1:%2:%3")
                              .arg ((int) (total / 3600.0), 2, 10, QLatin1Char ('0'))
                              .arg ((int) std::fmod (total / 60.0, 60.0), 2, 10, QLatin1Char ('0'))
                              .arg (std::fmod (total, 60.0), 6, 'f', 3, QLatin1Char ('0')));
    };
    connect (timeReference, &QLineEdit::textChanged, this, showSeconds);
    showSeconds();
    add (bwfForm, tag::bwfCodingHistory, tr ("Coding &history:"), true, 0);
    auto* fill = new QPushButton (tr ("&Fill In Now"));
    fill->setToolTip (tr ("Originator, a unique reference, and today's date and time"));
    connect (fill, &QPushButton::clicked, this, [originator, reference, date, time]
    {
        const auto now = QDateTime::currentDateTime();
        if (originator->text().isEmpty())
            originator->setText (QStringLiteral (APP_DISPLAY_NAME));
        if (reference->text().isEmpty())
            reference->setText (QStringLiteral ("PHX") + now.toString (QStringLiteral ("yyyyMMddHHmmss")));
        date->setText (now.toString (QStringLiteral ("yyyy-MM-dd")));
        time->setText (now.toString (QStringLiteral ("HH:mm:ss")));
    });
    bwfForm->addRow (QString(), fill);
    tabs->addTab (bwf, tr ("Broadcast Wave"));

    const auto ext = extension.toLower();
    QString kept;
    if (ext == QStringLiteral ("wav"))
        kept = tr ("Saved as WAV, every field is kept (tags in the LIST/INFO chunk, Broadcast Wave in bext).");
    else if (ext == QStringLiteral ("flac"))
        kept = tr ("Saved as FLAC, the tags are kept as Vorbis comments; Broadcast Wave fields are kept only in WAV.");
    else if (ext == QStringLiteral ("ogg"))
        kept = tr ("Saved as Ogg, title, artist, album, track, genre, date and comment are kept; the rest only in WAV or FLAC.");
    else
        kept = tr ("This file type does not keep metadata. Save as WAV or FLAC to keep it.");
    auto* about = new QLabel (kept, this);
    about->setWordWrap (true);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (tabs);
    layout->addWidget (about);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    fields.front().second->setFocus();
}

Tags MetadataDialog::tags() const
{
    auto result = original;   // keys this dialog does not show are kept
    for (const auto& [key, field] : fields)
    {
        QString value;
        if (auto* line = qobject_cast<QLineEdit*> (field))
            value = line->text();
        else if (auto* text = qobject_cast<QPlainTextEdit*> (field))
            value = text->toPlainText();
        value = value.trimmed();
        if (value.isEmpty())
            result.erase (key);
        else
            result[key] = value.toStdString();
    }
    return result;
}
