// SPDX-License-Identifier: AGPL-3.0-only
#include "SpeakerArrays.h"
#include "AmbiStudio.h"
#include "Settings.h"
#include "../core/SpatialStudio.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace speakerarrays
{
    namespace
    {
        const QString group = QStringLiteral ("speakerArrays");
    }

    QStringList names()
    {
        auto settings = appSettings();
        settings.beginGroup (group);
        auto all = settings.childKeys();
        all.sort (Qt::CaseInsensitive);
        return all;
    }

    std::vector<ambi::Speaker> get (const QString& name)
    {
        auto settings = appSettings();
        settings.beginGroup (group);
        std::vector<ambi::Speaker> speakers;
        std::string error;
        if (! settings.contains (name) || ! studio::parseSpeakers (settings.value (name).toString().toStdString(), speakers, error))
            return {};
        return speakers;
    }

    void put (const QString& name, const std::vector<ambi::Speaker>& speakers)
    {
        auto settings = appSettings();
        settings.beginGroup (group);
        settings.setValue (name, QString::fromStdString (studio::formatSpeakers (speakers)));
    }

    void remove (const QString& name)
    {
        auto settings = appSettings();
        settings.beginGroup (group);
        settings.remove (name);
    }
}

namespace
{
    enum Column { nameColumn, azimuthColumn, elevationColumn, lfeColumn };
}

SpeakerArraysDialog::SpeakerArraysDialog (const QString& initial, QWidget* parent) : QDialog (parent)
{
    setWindowTitle (tr ("Speaker Arrays"));
    resize (860, 520);

    list = new QListWidget (this);
    list->setAccessibleName (tr ("Your speaker arrays"));
    auto* listLabel = new QLabel (tr ("Your &arrays:"), this);
    listLabel->setBuddy (list);
    auto* newButton = new QPushButton (tr ("&New"), this);
    auto* deleteButton = new QPushButton (tr ("&Delete"), this);
    auto* examples = new QComboBox (this);
    examples->addItem (tr ("Start from an example..."));
    for (const auto& [name, speakers] : studio::exampleArrays())
        examples->addItem (QString::fromStdString (name));

    nameEdit = new QLineEdit (this);
    nameEdit->setPlaceholderText (tr ("My studio"));
    table = new QTableWidget (0, 4, this);
    table->setHorizontalHeaderLabels ({ tr ("Name"), tr ("Azim °"), tr ("Elev °"), tr ("Sub") });
    table->horizontalHeader()->setSectionResizeMode (QHeaderView::Stretch);
    table->setAccessibleName (tr ("Speakers, in the order of your output channels"));
    table->setToolTip (tr ("One row per output channel, in order. Azimuth: 0 ahead, 90 right, -90 left, 180 behind. Elevation: 0 ear height, 90 overhead"));
    auto* addButton = new QPushButton (tr ("Add &Speaker"), this);
    auto* removeButton = new QPushButton (tr ("&Remove Speaker"), this);
    auto* saveButton = new QPushButton (tr ("Sa&ve Array"), this);
    view = new StudioHud (this);
    view->setToolTip (tr ("Your array from above (front at the top) and from the side"));
    status = new QLabel (this);
    status->setWordWrap (true);

    auto* left = new QVBoxLayout;
    left->addWidget (listLabel);
    left->addWidget (list, 1);
    auto* listButtons = new QHBoxLayout;
    listButtons->addWidget (newButton);
    listButtons->addWidget (deleteButton);
    left->addLayout (listButtons);
    left->addWidget (examples);

    auto* form = new QFormLayout;
    form->addRow (tr ("&Name:"), nameEdit);
    auto* right = new QVBoxLayout;
    right->addLayout (form);
    auto* middle = new QHBoxLayout;
    auto* tableSide = new QVBoxLayout;
    tableSide->addWidget (table, 1);
    auto* rowButtons = new QHBoxLayout;
    rowButtons->addWidget (addButton);
    rowButtons->addWidget (removeButton);
    rowButtons->addStretch (1);
    rowButtons->addWidget (saveButton);
    tableSide->addLayout (rowButtons);
    middle->addLayout (tableSide, 3);
    middle->addWidget (view, 2);
    right->addLayout (middle, 1);
    right->addWidget (status);

    auto* columns = new QHBoxLayout;
    columns->addLayout (left, 1);
    columns->addLayout (right, 3);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout (this);
    auto* about = new QLabel (tr ("Describe your speakers, up to 16 channels, in the order of your audio interface's outputs. "
                                  "The ambisonic decoders then list the array as Custom: its name, and fit an AllRAD decoder (max-rE) to it. "
                                  "Subwoofer rows get silence for your bass management."), this);
    about->setWordWrap (true);
    layout->addWidget (about);
    layout->addLayout (columns, 1);
    layout->addWidget (buttons);

    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (list, &QListWidget::currentTextChanged, this, [this] (const QString& name)
    {
        if (! updating && ! name.isEmpty())
            showArray (name, speakerarrays::get (name));
    });
    connect (newButton, &QPushButton::clicked, this, [this]
    {
        list->clearSelection();
        showArray ({}, {});
        nameEdit->setFocus();
    });
    connect (deleteButton, &QPushButton::clicked, this, [this]
    {
        const auto name = list->currentItem() != nullptr ? list->currentItem()->text() : QString();
        if (name.isEmpty())
            return;
        if (QMessageBox::question (this, tr ("Delete Speaker Array"), tr ("Delete %1?").arg (name)) != QMessageBox::Yes)
            return;
        speakerarrays::remove (name);
        if (current == name)
            current.clear();
        loadList();
        showArray ({}, {});
    });
    connect (examples, &QComboBox::activated, this, [this, examples] (int index)
    {
        if (index <= 0)
            return;
        const auto all = studio::exampleArrays();
        const auto& [name, speakers] = all[(size_t) index - 1];
        list->clearSelection();
        showArray (QString::fromStdString (name), speakers);
        examples->setCurrentIndex (0);
        status->setText (tr ("An example: change it to match your room, name it and save it."));
    });
    connect (addButton, &QPushButton::clicked, this, [this]
    {
        if (table->rowCount() >= studio::maxSpeakers)
        {
            status->setText (tr ("16 speakers is the most PHINYX plays"));
            return;
        }
        addRow ({ std::to_string (table->rowCount() + 1), 0.0, 0.0, false });
        table->setCurrentCell (table->rowCount() - 1, nameColumn);
        refreshView();
    });
    connect (removeButton, &QPushButton::clicked, this, [this]
    {
        if (table->currentRow() >= 0)
            table->removeRow (table->currentRow());
        refreshView();
    });
    connect (saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect (table, &QTableWidget::cellChanged, this, [this] { if (! updating) refreshView(); });

    loadList();
    const auto start = initial.isEmpty() ? (list->count() > 0 ? list->item (0)->text() : QString()) : initial;
    if (! start.isEmpty())
    {
        const auto found = list->findItems (start, Qt::MatchExactly);
        if (! found.isEmpty())
            list->setCurrentItem (found.first());
        showArray (start, speakerarrays::get (start));
    }
    else
    {
        const auto first = studio::exampleArrays().front();
        showArray ({}, first.second);
        status->setText (tr ("No arrays yet: here is an octagon to start from. Name it and save it."));
    }
}

void SpeakerArraysDialog::loadList()
{
    updating = true;
    list->clear();
    list->addItems (speakerarrays::names());
    updating = false;
}

void SpeakerArraysDialog::showArray (const QString& name, const std::vector<ambi::Speaker>& speakers)
{
    updating = true;
    nameEdit->setText (name);
    table->setRowCount (0);
    for (const auto& sp : speakers)
        addRow (sp);
    updating = false;
    refreshView();
}

void SpeakerArraysDialog::addRow (const ambi::Speaker& sp)
{
    const bool was = updating;
    updating = true;
    const int row = table->rowCount();
    table->insertRow (row);
    table->setItem (row, nameColumn, new QTableWidgetItem (QString::fromStdString (sp.name)));
    table->setItem (row, azimuthColumn, new QTableWidgetItem (QString::number (sp.azimuth, 'g', 6)));
    table->setItem (row, elevationColumn, new QTableWidgetItem (QString::number (sp.elevation, 'g', 6)));
    auto* lfe = new QTableWidgetItem();
    lfe->setFlags (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    lfe->setCheckState (sp.lfe ? Qt::Checked : Qt::Unchecked);
    table->setItem (row, lfeColumn, lfe);
    updating = was;
}

std::vector<ambi::Speaker> SpeakerArraysDialog::fromTable (QString& error) const
{
    // Through the text form, so the same checks apply as to a saved array.
    QString text;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        const auto cell = [&] (int column) { return table->item (row, column) != nullptr ? table->item (row, column)->text().trimmed() : QString(); };
        auto name = cell (nameColumn);
        name.replace (QLatin1Char (' '), QLatin1Char ('_'));
        if (name.isEmpty())
            name = QString::number (row + 1);
        const bool lfe = table->item (row, lfeColumn) != nullptr && table->item (row, lfeColumn)->checkState() == Qt::Checked;
        text += QStringLiteral ("%1 %2 %3%4\n").arg (name, cell (azimuthColumn).isEmpty() ? QStringLiteral ("0") : cell (azimuthColumn),
                                                      cell (elevationColumn).isEmpty() ? QStringLiteral ("0") : cell (elevationColumn),
                                                      lfe ? QStringLiteral (" LFE") : QString());
    }
    std::vector<ambi::Speaker> speakers;
    std::string why;
    if (! studio::parseSpeakers (text.toStdString(), speakers, why))
    {
        error = QString::fromStdString (why);
        return {};
    }
    return speakers;
}

void SpeakerArraysDialog::refreshView()
{
    QString error;
    const auto speakers = fromTable (error);
    std::vector<studio::Dot> dots;
    int index = 0;
    for (const auto& sp : speakers)
        if (! sp.lfe)
            dots.push_back ({ (float) sp.azimuth, (float) sp.elevation, 1.0f, 1.0f, index++ });
    view->setDots ({ dots }, 0, false);
    if (! error.isEmpty())
        status->setText (error);
    else
    {
        const auto lfe = std::count_if (speakers.begin(), speakers.end(), [] (const ambi::Speaker& sp) { return sp.lfe; });
        status->setText (lfe > 0 ? tr ("%1 channels: %2 speakers and %3 subwoofer(s)").arg (speakers.size()).arg ((long long) speakers.size() - lfe).arg (lfe)
                                 : tr ("%1 channels").arg (speakers.size()));
    }
}

bool SpeakerArraysDialog::save()
{
    const auto name = nameEdit->text().trimmed().remove (QLatin1Char ('/')).remove (QLatin1Char ('\\'));
    if (name.isEmpty())
    {
        status->setText (tr ("Give the array a name first"));
        nameEdit->setFocus();
        return false;
    }
    QString error;
    const auto speakers = fromTable (error);
    if (! error.isEmpty())
    {
        status->setText (error);
        return false;
    }
    speakerarrays::put (name, speakers);
    current = name;
    loadList();
    const auto found = list->findItems (name, Qt::MatchExactly);
    updating = true;
    if (! found.isEmpty())
        list->setCurrentItem (found.first());
    updating = false;
    status->setText (tr ("Saved %1: choose Custom: %1 as the decoder").arg (name));
    return true;
}
