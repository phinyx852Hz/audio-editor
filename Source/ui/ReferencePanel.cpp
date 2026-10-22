// SPDX-License-Identifier: AGPL-3.0-only
#include "ReferencePanel.h"

#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QUrl>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ReferencePanel::ReferencePanel (QWidget* parent) : QWidget (parent)
{
    setAcceptDrops (true);
    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (6, 6, 6, 6);

    list = new QListWidget (this);
    list->setAccessibleName (tr ("Reference tracks"));
    list->installEventFilter (this);
    layout->addWidget (list, 1);

    auto* offsetRow = new QHBoxLayout;
    auto* offsetLabel = new QLabel (tr ("Level &offset:"), this);
    offset = new QDoubleSpinBox (this);
    offset->setRange (-24.0, 24.0);
    offset->setSingleStep (0.5);
    offset->setDecimals (1);
    offset->setSuffix (tr (" dB"));
    offset->setToolTip (tr ("Added to the loudness match, if you want this reference louder or quieter"));
    offsetLabel->setBuddy (offset);
    offsetRow->addWidget (offsetLabel);
    offsetRow->addWidget (offset, 1);
    layout->addLayout (offsetRow);

    clean = new QCheckBox (tr ("Hear references &without the Master Section"), this);
    clean->setToolTip (tr ("References are usually finished masters: hear them as they are, in time with your audio"));
    layout->addWidget (clean);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton (tr ("&Add..."), this);
    auto* remove = new QPushButton (tr ("&Remove"), this);
    auto* match = new QPushButton (tr ("Match &Loudness"), this);
    match->setToolTip (tr ("Measure your audio again, as it sounds through the Master Section, and match the references to it"));
    buttons->addWidget (add);
    buttons->addWidget (remove);
    buttons->addStretch (1);
    buttons->addWidget (match);
    layout->addLayout (buttons);

    status = new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);

    connect (add, &QPushButton::clicked, this, &ReferencePanel::addRequested);
    connect (remove, &QPushButton::clicked, this, [this] { if (currentRow() >= 0) Q_EMIT removeRequested (currentRow()); });
    connect (match, &QPushButton::clicked, this, &ReferencePanel::matchRequested);
    connect (list, &QListWidget::currentRowChanged, this, [this] (int row) { if (! updating && row >= 0) Q_EMIT chosen (row); });
    connect (list, &QListWidget::itemActivated, this, [this] { Q_EMIT hearRequested (currentRow()); });
    connect (offset, &QDoubleSpinBox::valueChanged, this, [this] (double db) { if (! updating && currentRow() >= 0) Q_EMIT offsetChanged (currentRow(), db); });
    connect (clean, &QCheckBox::toggled, this, [this] (bool on) { if (! updating) Q_EMIT cleanChanged (on); });
}

void ReferencePanel::setRows (const QList<Row>& rows, int current, bool hearing)
{
    updating = true;
    list->clear();
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto playing = hearing && i == current;
        auto* item = new QListWidgetItem ((playing ? QStringLiteral ("▶ ") : QString()) + rows[i].name
                                          + (rows[i].detail.isEmpty() ? QString() : QStringLiteral ("\n    ") + rows[i].detail), list);
        auto font = item->font();
        font.setBold (playing);
        item->setFont (font);
    }
    if (current >= 0 && current < rows.size())
    {
        list->setCurrentRow (current);
        offset->setValue (rows[current].offsetDb);
    }
    offset->setEnabled (current >= 0 && current < rows.size());
    updating = false;
}

void ReferencePanel::setClean (bool on)
{
    updating = true;
    clean->setChecked (on);
    updating = false;
}

void ReferencePanel::setStatus (const QString& text) { status->setText (text); }
int ReferencePanel::currentRow() const               { return list->currentRow(); }
QWidget* ReferencePanel::focusTarget() const         { return list; }

bool ReferencePanel::eventFilter (QObject* o, QEvent* e)
{
    if (o == list && e->type() == QEvent::KeyPress)
    {
        const auto* key = static_cast<QKeyEvent*> (e);
        const auto row = currentRow();
        switch (key->key())
        {
            case Qt::Key_Insert: Q_EMIT addRequested(); return true;
            case Qt::Key_Delete: if (row >= 0) Q_EMIT removeRequested (row); return true;
            case Qt::Key_Plus:
            case Qt::Key_Equal:  offset->stepUp(); return true;
            case Qt::Key_Minus:  offset->stepDown(); return true;
            default: break;
        }
    }
    return QWidget::eventFilter (o, e);
}

void ReferencePanel::dragEnterEvent (QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void ReferencePanel::dropEvent (QDropEvent* e)
{
    QStringList paths;
    for (const auto& url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (paths.isEmpty())
        return;
    e->acceptProposedAction();
    QTimer::singleShot (0, this, [this, paths] { Q_EMIT filesDropped (paths); });
}
