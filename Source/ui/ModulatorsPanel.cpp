// SPDX-License-Identifier: AGPL-3.0-only
#include "ModulatorsPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    const QColor gold (0xc8, 0xa2, 0x4a);
    constexpr int sliderSteps = 1000;

    QString utf8 (const char* s) { return QString::fromUtf8 (s); }

    // A control's value from a slider position (0..1), and back: logarithmic ones by ratio.
    float fromPosition (const modrack::Control& c, double t)
    {
        if (c.logarithmic && c.low > 0.0f)
            return (float) (c.low * std::pow ((double) c.high / c.low, t));
        return (float) (c.low + (c.high - c.low) * t);
    }

    double toPosition (const modrack::Control& c, float v)
    {
        if (c.logarithmic && c.low > 0.0f)
            return std::log (std::max (v, c.low) / c.low) / std::log ((double) c.high / c.low);
        return (v - c.low) / std::max (1.0e-9f, c.high - c.low);
    }

    QString valueText (const modrack::Control& c, float v)
    {
        const auto unit = utf8 (c.unit);
        if (unit.isEmpty() && c.low >= -1.0f && c.high <= 1.0f)
            return QString::number (v * 100.0f, 'f', 1) + QStringLiteral (" %");
        const auto decimals = std::abs (v) < 10.0f ? 3 : std::abs (v) < 100.0f ? 1 : 0;
        return QString::number (v, 'f', decimals) + (unit.isEmpty() ? QString() : QStringLiteral (" ") + unit);
    }
}

// What the chosen modulator has given over the last few seconds, in gold.
class ModulatorTrace final : public QWidget
{
public:
    explicit ModulatorTrace (QWidget* parent) : QWidget (parent) { setMinimumSize (180, 55); }

    void push (float v)
    {
        values.push_back (v);
        while (values.size() > 233)
            values.pop_front();
        update();
    }
    void clear() { values.clear(); update(); }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::Antialiasing);
        const QRectF r = QRectF (rect()).adjusted (0.5, 0.5, -0.5, -0.5);
        g.setPen (palette().color (QPalette::Mid));
        g.setBrush (palette().color (QPalette::Base));
        g.drawRoundedRect (r, 4, 4);
        if (values.size() < 2)
            return;
        QPainterPath path;
        const auto inner = r.adjusted (3, 3, -3, -3);
        for (size_t i = 0; i < values.size(); ++i)
        {
            const QPointF p (inner.left() + inner.width() * (double) i / 232.0, inner.bottom() - inner.height() * std::clamp (values[i], 0.0f, 1.0f));
            i == 0 ? path.moveTo (p) : path.lineTo (p);
        }
        g.setPen (QPen (gold, 2.0));
        g.setBrush (Qt::NoBrush);
        g.drawPath (path);
    }

private:
    std::deque<float> values;
};

ModulatorsPanel::ModulatorsPanel (AudioEngine& e, QWidget* parent) : QWidget (parent), engine (e)
{
    setAccessibleName (tr ("Modulators"));
    auto* layout = new QHBoxLayout (this);
    layout->setContentsMargins (6, 6, 6, 6);
    layout->setSpacing (8);

    // The rack.
    auto* left = new QVBoxLayout;
    list = new QListWidget (this);
    list->setAccessibleName (tr ("Modulators"));
    list->setMinimumWidth (180);
    left->addWidget (list, 1);
    auto* leftButtons = new QHBoxLayout;
    addButton = new QPushButton (tr ("Add Modulator"), this);
    auto* kinds = new QMenu (addButton);
    for (int k = 0; k < modrack::numKinds; ++k)
    {
        const auto kind = (modrack::Kind) k;
        kinds->addAction (utf8 (modrack::kindName (kind)), this, [this, kind] { addModulator (kind); });
    }
    addButton->setMenu (kinds);
    removeButton = new QPushButton (tr ("Remove"), this);
    renameButton = new QPushButton (tr ("Rename"), this);
    leftButtons->addWidget (addButton);
    leftButtons->addWidget (renameButton);
    leftButtons->addWidget (removeButton);
    left->addLayout (leftButtons);
    layout->addLayout (left, 2);

    // The chosen one's controls and trace.
    auto* middle = new QVBoxLayout;
    heading = new QLabel (this);
    auto hf = heading->font();
    hf.setBold (true);
    heading->setFont (hf);
    middle->addWidget (heading);
    trace = new ModulatorTrace (this);
    middle->addWidget (trace);
    controlArea = new QWidget (this);
    middle->addWidget (controlArea, 1);
    layout->addLayout (middle, 4);

    // Where it goes.
    auto* right = new QVBoxLayout;
    destinations = new QTreeWidget (this);
    destinations->setAccessibleName (tr ("Destinations"));
    destinations->setColumnCount (4);
    destinations->setHeaderLabels ({ tr ("Plugin"), tr ("Parameter"), tr ("Low"), tr ("High") });
    destinations->setRootIsDecorated (false);
    destinations->header()->setSectionResizeMode (QHeaderView::ResizeToContents);
    destinations->header()->setSectionResizeMode (1, QHeaderView::Stretch);
    destinations->header()->setStretchLastSection (false);
    right->addWidget (destinations, 1);
    auto* rightButtons = new QHBoxLayout;
    addRouteButton = new QPushButton (tr ("Add Destination..."), this);
    removeRouteButton = new QPushButton (tr ("Remove Destination"), this);
    rightButtons->addWidget (addRouteButton);
    rightButtons->addWidget (removeRouteButton);
    right->addLayout (rightButtons);
    layout->addLayout (right, 4);

    connect (list, &QListWidget::currentRowChanged, this, [this] (int)
    {
        trace->clear();
        buildControls();
        rebuildRoutes();
    });
    connect (removeButton, &QPushButton::clicked, this, [this] { removeModulator(); });
    connect (renameButton, &QPushButton::clicked, this, [this] { renameModulator(); });
    connect (addRouteButton, &QPushButton::clicked, this, [this] { chooseDestination(); });
    connect (removeRouteButton, &QPushButton::clicked, this, [this] { removeRoute(); });

    auto* timer = new QTimer (this);
    connect (timer, &QTimer::timeout, this, [this] { tick(); });
    timer->start (33);

    refresh();
}

int ModulatorsPanel::chosen() const
{
    const auto row = list->currentRow();
    return row >= 0 && row < (int) rack.size() ? row : -1;
}

void ModulatorsPanel::refresh()
{
    rack = engine.modulators();
    routes = engine.modulatorRoutes();
    rebuildList();
}

void ModulatorsPanel::rebuildList()
{
    const auto keep = list->currentRow();
    {
        const QSignalBlocker block (list);
        list->clear();
        for (size_t i = 0; i < rack.size(); ++i)
            list->addItem (QStringLiteral ("M%1  %2").arg (i + 1).arg (QString::fromStdString (rack[i].name)));
        list->setCurrentRow (rack.empty() ? -1 : std::clamp (keep, 0, (int) rack.size() - 1));
    }
    addButton->setEnabled ((int) rack.size() < modrack::maxModulators);
    buildControls();
    rebuildRoutes();
}

void ModulatorsPanel::buildControls()
{
    for (auto* child : controlArea->findChildren<QWidget*> (Qt::FindDirectChildrenOnly))
    {
        child->hide();
        child->deleteLater();
    }
    delete controlArea->layout();
    valueLabels.clear();

    const auto m = chosen();
    removeButton->setEnabled (m >= 0);
    renameButton->setEnabled (m >= 0);
    addRouteButton->setEnabled (m >= 0);
    if (m < 0)
    {
        heading->setText (tr ("Add a modulator, then give it destinations: parameters of the plugins in the Device Chain."));
        heading->setWordWrap (true);
        return;
    }
    heading->setWordWrap (false);
    heading->setText (QStringLiteral ("M%1  %2 (%3)").arg (m + 1).arg (QString::fromStdString (rack[(size_t) m].name),
                                                                     utf8 (modrack::kindName (rack[(size_t) m].kind))));

    auto* grid = new QGridLayout (controlArea);
    grid->setContentsMargins (0, 0, 0, 0);
    grid->setHorizontalSpacing (6);
    grid->setVerticalSpacing (2);
    const auto& controls = modrack::controls (rack[(size_t) m].kind);
    // Two columns of controls when there are many (the Step Modulator's sixteen steps).
    const auto perColumn = controls.size() > 8 ? (int) (controls.size() + 1) / 2 : (int) controls.size();
    for (int i = 0; i < (int) controls.size() && i < modrack::maxControls; ++i)
    {
        const auto& c = controls[(size_t) i];
        const auto row = i % perColumn, column = (i / perColumn) * 3;
        auto* name = new QLabel (utf8 (c.name), controlArea);
        grid->addWidget (name, row, column);
        auto& value = rack[(size_t) m].values[(size_t) i];
        if (! c.choices.empty() || c.source)
        {
            auto* box = new QComboBox (controlArea);
            box->setAccessibleName (utf8 (c.name));
            if (c.source)
            {
                box->addItem (rack[(size_t) m].kind == modrack::Kind::sampleHold ? tr ("Noise") : tr ("None"));
                for (size_t k = 0; k < rack.size(); ++k)
                    box->addItem (QStringLiteral ("M%1  %2").arg (k + 1).arg (QString::fromStdString (rack[k].name)));
            }
            else
                for (const auto* choice : c.choices)
                    box->addItem (utf8 (choice));
            box->setCurrentIndex (std::clamp ((int) std::lround (value), 0, box->count() - 1));
            connect (box, &QComboBox::currentIndexChanged, this, [this, m, i] (int index)
            {
                if (m < (int) rack.size())
                {
                    rack[(size_t) m].values[(size_t) i] = (float) index;
                    push();
                }
            });
            grid->addWidget (box, row, column + 1, 1, 2);
            valueLabels.push_back (nullptr);
        }
        else
        {
            auto* slider = new QSlider (Qt::Horizontal, controlArea);
            slider->setAccessibleName (utf8 (c.name));
            slider->setRange (0, sliderSteps);
            slider->setValue ((int) std::lround (toPosition (c, value) * sliderSteps));
            auto* shown = new QLabel (valueText (c, value), controlArea);
            shown->setMinimumWidth (70);
            connect (slider, &QSlider::valueChanged, this, [this, m, i, shown] (int position)
            {
                if (m >= (int) rack.size())
                    return;
                const auto& spec = modrack::controls (rack[(size_t) m].kind)[(size_t) i];
                const auto v = fromPosition (spec, (double) position / sliderSteps);
                rack[(size_t) m].values[(size_t) i] = v;
                shown->setText (valueText (spec, v));
                push();
            });
            grid->addWidget (slider, row, column + 1);
            grid->addWidget (shown, row, column + 2);
            valueLabels.push_back (shown);
        }
    }
    grid->setRowStretch (perColumn, 1);
}

void ModulatorsPanel::rebuildRoutes()
{
    const QSignalBlocker block (destinations);
    destinations->clear();
    const auto m = chosen();
    for (size_t r = 0; r < routes.size(); ++r)
    {
        const auto& route = routes[r];
        if (route.modulator != m)
            continue;
        const auto slot = engine.masterSlot (route.slot);
        const auto live = engine.modulatorRouteLive ((int) r);
        auto* item = new QTreeWidgetItem (destinations);
        item->setData (0, Qt::UserRole, (int) r);
        item->setText (0, QStringLiteral ("%1 · %2").arg (route.slot + 1).arg (live ? QString::fromStdString (slot.name) : tr ("(gone)")));
        item->setText (1, live ? QString::fromStdString (engine.chainParameter (ChainPath { route.slot, {} }, route.parameter).name)
                               : QString::number (route.parameter + 1));
        for (int end = 0; end < 2; ++end)
        {
            auto* spin = new QDoubleSpinBox (destinations);
            spin->setRange (0.0, 100.0);
            spin->setDecimals (1);
            spin->setSuffix (QStringLiteral (" %"));
            spin->setButtonSymbols (QAbstractSpinBox::NoButtons);
            spin->setFixedWidth (64);
            spin->setAccessibleName (end == 0 ? tr ("Low") : tr ("High"));
            spin->setValue ((end == 0 ? route.low : route.high) * 100.0);
            connect (spin, &QDoubleSpinBox::valueChanged, this, [this, r, end] (double v)
            {
                if (r < routes.size())
                {
                    (end == 0 ? routes[r].low : routes[r].high) = (float) (v / 100.0);
                    engine.setModulatorRoutes (routes);
                }
            });
            destinations->setItemWidget (item, 2 + end, spin);
        }
    }
    removeRouteButton->setEnabled (destinations->topLevelItemCount() > 0);
}

void ModulatorsPanel::addModulator (modrack::Kind kind)
{
    if ((int) rack.size() >= modrack::maxModulators)
        return;
    rack.push_back (modrack::make (kind));
    push();
    rebuildList();
    list->setCurrentRow ((int) rack.size() - 1);
}

void ModulatorsPanel::removeModulator()
{
    const auto m = chosen();
    if (m < 0)
        return;
    rack.erase (rack.begin() + m);
    // Its routes go; later modulators move down one, and sources that picked it pick none.
    routes.erase (std::remove_if (routes.begin(), routes.end(), [m] (const modrack::Route& r) { return r.modulator == m; }), routes.end());
    for (auto& r : routes)
        if (r.modulator > m)
            --r.modulator;
    for (auto& other : rack)
    {
        const auto& controls = modrack::controls (other.kind);
        for (size_t i = 0; i < controls.size(); ++i)
            if (controls[i].source)
            {
                auto& v = other.values[i];
                const auto picked = (int) std::lround (v) - 1;
                v = picked == m ? 0.0f : picked > m ? v - 1.0f : v;
            }
    }
    push();
    engine.setModulatorRoutes (routes);
    rebuildList();
}

void ModulatorsPanel::renameModulator()
{
    const auto m = chosen();
    if (m < 0)
        return;
    bool ok = false;
    const auto name = QInputDialog::getText (this, tr ("Rename Modulator"), tr ("Name:"), QLineEdit::Normal,
                                             QString::fromStdString (rack[(size_t) m].name), &ok).trimmed();
    if (! ok || name.isEmpty())
        return;
    rack[(size_t) m].name = name.toStdString();
    push();
    rebuildList();
}

void ModulatorsPanel::chooseDestination()
{
    const auto m = chosen();
    if (m < 0 || (int) routes.size() >= modrack::maxRoutes)
        return;
    QMenu menu (this);
    for (int slot = 0; slot < AudioEngine::numMasterSlots; ++slot)
    {
        const auto info = engine.masterSlot (slot);
        if (info.empty || info.failed)
            continue;
        auto* sub = menu.addMenu (QStringLiteral ("%1 · %2").arg (slot + 1).arg (QString::fromStdString (info.name)));
        const auto params = engine.chainParameters (ChainPath { slot, {} });
        for (int p = 0; p < (int) params.size(); ++p)
            sub->addAction (QString::fromStdString (params[(size_t) p].name), this, [this, m, slot, p, id = info.pluginId]
            {
                modrack::Route r;
                r.modulator = m;
                r.slot = slot;
                r.parameter = p;
                r.pluginId = id;
                routes.push_back (r);
                engine.setModulatorRoutes (routes);
                rebuildRoutes();
            });
    }
    if (menu.isEmpty())
        menu.addAction (tr ("Add a plugin to the Device Chain first"))->setEnabled (false);
    menu.exec (addRouteButton->mapToGlobal (QPoint (0, addRouteButton->height())));
}

void ModulatorsPanel::removeRoute()
{
    auto* item = destinations->currentItem();
    if (item == nullptr && destinations->topLevelItemCount() > 0)
        item = destinations->topLevelItem (0);
    if (item == nullptr)
        return;
    const auto r = item->data (0, Qt::UserRole).toInt();
    if (r >= 0 && r < (int) routes.size())
    {
        routes.erase (routes.begin() + r);
        engine.setModulatorRoutes (routes);
    }
    rebuildRoutes();
}

void ModulatorsPanel::push()
{
    engine.setModulators (rack);
}

void ModulatorsPanel::tick()
{
    if (! isVisible())
        return;
    const auto m = chosen();
    if (m >= 0)
        trace->push (engine.modulatorValue (m));
}
