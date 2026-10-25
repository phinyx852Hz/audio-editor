// SPDX-License-Identifier: AGPL-3.0-only
#include "PrismEditor.h"
#include "PhiWindow.h"
#include "ChainRow.h"
#include "MasterPanel.h"
#include "Potentiometer.h"
#include "Settings.h"
#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QSizeGrip>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int resolution = 1000;

    QToolButton* button (const QString& text, const QString& tip, QWidget* parent, bool checkable = false)
    {
        auto* b = new QToolButton (parent);
        b->setText (text);
        b->setToolTip (tip);
        b->setAccessibleName (tip);
        b->setCheckable (checkable);
        b->setFocusPolicy (Qt::NoFocus);
        b->setAutoRaise (true);
        return b;
    }

    QLabel* small (const QString& text, QWidget* parent, bool dim = false)
    {
        auto* l = new QLabel (text, parent);
        auto f = l->font();
        f.setPointSizeF (f.pointSizeF() * 0.85);
        l->setFont (f);
        if (dim)
            l->setForegroundRole (QPalette::PlaceholderText);
        return l;
    }

    // Each ray's colour, stepping through a spectrum as light leaves a prism.
    QColor rayColour (int ray)
    {
        return QColor::fromHsvF (std::fmod (0.62 - 0.11 * ray + 1.0, 1.0), 0.55, 0.9);
    }

    QString percent (float v) { return QString::number (std::lround (v * 100.0f)) + QLatin1Char ('%'); }
}

//==============================================================================
PrismEditor::PrismEditor (AudioEngine& e, int s, std::uint64_t id, QWidget* window)
    : QFrame (window), engine (e), slot (s), uid (id)
{
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (tr ("Prism editor"));
    setAccessibleDescription (tr ("Left and Right pick a device, Up and Down a ray, Insert adds a plugin, Delete removes it, "
                                  "Enter opens it, Control G groups, M mutes and S solos the ray, Escape docks the Prism."));
    auto* shadow = new QGraphicsDropShadowEffect (this);
    shadow->setBlurRadius (30);
    shadow->setOffset (0, 8);
    shadow->setColor (QColor (0, 0, 0, 150));
    setGraphicsEffect (shadow);

    auto* outer = new QVBoxLayout (this);
    outer->setContentsMargins (12, 10, 12, 8);
    outer->setSpacing (6);

    // Header: the way in, the name, the split. Dragging it moves the editor.
    header = new QWidget (this);
    header->setCursor (Qt::OpenHandCursor);
    header->installEventFilter (this);
    auto* top = new QHBoxLayout (header);
    top->setContentsMargins (0, 0, 0, 0);
    top->setSpacing (6);
    crumbs = new QHBoxLayout;
    crumbs->setSpacing (2);
    top->addLayout (crumbs);

    nameEdit = new QLineEdit (header);
    nameEdit->setFrame (false);
    nameEdit->setFixedWidth (170);
    nameEdit->setAccessibleName (tr ("Prism name"));
    nameEdit->setToolTip (tr ("The Prism's name (F2)"));
    nameEdit->setFont ([&] { auto f = nameEdit->font(); f.setBold (true); return f; }());
    connect (nameEdit, &QLineEdit::editingFinished, this, [this]
    {
        engine.prismRename (prismPath(), nameEdit->text().toStdString());
        setFocus (Qt::OtherFocusReason);
        Q_EMIT chainChanged();
    });
    top->addWidget (nameEdit);
    top->addStretch (1);

    top->addWidget (small (tr ("Split"), header, true));
    splitBox = new QComboBox (header);
    splitBox->addItems ({ tr ("Parallel"), tr ("Bands"), tr ("Mid/Side"), tr ("Left/Right") });
    splitBox->setFocusPolicy (Qt::NoFocus);
    splitBox->setAccessibleName (tr ("How the Prism divides the sound among its rays"));
    splitBox->setToolTip (tr ("Parallel: every ray hears everything, and they are added together.\n"
                              "Bands: ray 1 is the lowest band, ray 2 the next (up to four).\n"
                              "Mid/Side: ray 1 hears the mid, ray 2 the side.\n"
                              "Left/Right: ray 1 hears the left, ray 2 the right."));
    connect (splitBox, &QComboBox::activated, this, [this] (int index)
    {
        engine.prismSetSplit (prismPath(), index);
        rebuild();
        setFocus (Qt::OtherFocusReason);
        Q_EMIT chainChanged();
    });
    top->addWidget (splitBox);

    for (int i = 0; i < 3; ++i)
    {
        auto* pot = new Potentiometer (header);
        pot->setFixedSize (26, 26);
        pot->setRange (0, resolution);
        pot->setFocusPolicy (Qt::NoFocus);
        pot->setAccessibleName (tr ("Crossover %1").arg (i + 1));
        pot->setToolTip (tr ("Crossover %1 between bands %1 and %2").arg (i + 1).arg (i + 2));
        auto* label = small ({}, header);
        label->setFixedWidth (52);
        connect (pot, &QDial::valueChanged, this, [this, i] (int v)
        {
            if (crossoverPots[(size_t) i]->isSliderDown())
                engine.setChainParameter (prismPath(), PrismInfo::numMacros + 1 + i, (float) v / resolution);
        });
        top->addWidget (pot);
        top->addWidget (label);
        crossoverPots.push_back (pot);
        crossoverLabels.push_back (label);
    }

    auto* rayButton = button (tr ("+ Ray"), tr ("Add a ray (Ctrl+T)"), header);
    connect (rayButton, &QToolButton::clicked, this, &PrismEditor::addRay);
    top->addWidget (rayButton);
    mapButton = button (tr ("Map"), tr ("Map: turn a macro, then click a knob in the rays to put it on that macro"), header, true);
    connect (mapButton, &QToolButton::toggled, this, [this] (bool on)
    {
        if (! on)
            armed = -1;
        Q_EMIT message (on ? tr ("Map: turn or click a macro, then click any knob in the rays") : tr ("Map off"));
        sync();
    });
    top->addWidget (mapButton);
    auto* dockButton = button (tr ("Dock"), tr ("Put the Prism back in its card (Esc)"), header);
    connect (dockButton, &QToolButton::clicked, this, &PrismEditor::dock);
    top->addWidget (dockButton);
    outer->addWidget (header);

    // The macros.
    auto* macros = new QHBoxLayout;
    macros->setSpacing (4);
    buildMacros (macros);
    outer->addLayout (macros);

    // The rays.
    raysScroll = new QScrollArea (this);
    raysScroll->setWidgetResizable (true);
    raysScroll->setFrameShape (QFrame::NoFrame);
    raysScroll->setFocusPolicy (Qt::NoFocus);
    raysScroll->viewport()->setAutoFillBackground (false);
    raysHost = new QWidget;
    raysHost->setAutoFillBackground (false);
    raysLayout = new QVBoxLayout (raysHost);
    raysLayout->setContentsMargins (0, 0, 0, 0);
    raysLayout->setSpacing (4);
    raysScroll->setWidget (raysHost);
    outer->addWidget (raysScroll, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget (small (tr ("Shift+Left/Right picks devices, Ctrl+G groups them, Ctrl+T adds a ray, Esc docks"), this, true), 1);
    bottom->addWidget (new QSizeGrip (this), 0, Qt::AlignBottom | Qt::AlignRight);
    outer->addLayout (bottom);

    connect (&timer, &QTimer::timeout, this, [this]
    {
        if (closing)
            return;
        if (! follow())
        {
            dockNow();
            return;
        }
        if (signature (engine.prismInfo (prismPath())) != lastSignature)
            rebuild();
        else
            sync();
    });
    timer.start (100);
    rebuild();
}

void PrismEditor::buildMacros (QHBoxLayout* row)
{
    for (int m = 0; m < PrismInfo::numMacros; ++m)
    {
        MacroView v;
        v.column = new QWidget (this);
        v.column->setContextMenuPolicy (Qt::CustomContextMenu);
        v.column->setToolTip (tr ("Right-click: rename, MIDI learn, mappings and their ranges"));
        auto* col = new QVBoxLayout (v.column);
        col->setContentsMargins (2, 0, 2, 0);
        col->setSpacing (0);
        v.pot = new Potentiometer (v.column);
        v.pot->setFixedSize (44, 44);
        v.pot->setRange (0, resolution);
        v.pot->setFocusPolicy (Qt::NoFocus);
        v.name = small ({}, v.column);
        v.name->setAlignment (Qt::AlignHCenter);
        v.name->setFixedWidth (78);
        v.name->installEventFilter (this);
        v.name->setProperty ("macro", m);
        v.value = small ({}, v.column, true);
        v.value->setAlignment (Qt::AlignHCenter);
        v.midi = small ({}, v.column, true);
        v.midi->setAlignment (Qt::AlignHCenter);
        col->addWidget (v.pot, 0, Qt::AlignHCenter);
        col->addWidget (v.name);
        col->addWidget (v.value);
        col->addWidget (v.midi);
        row->addWidget (v.column);

        connect (v.pot, &QDial::valueChanged, this, [this, m] (int position)
        {
            if (macroViews[(size_t) m].pot->isSliderDown())
                engine.setChainParameter (prismPath(), m, (float) position / resolution);
        });
        connect (v.pot, &QDial::sliderPressed, this, [this, m]
        {
            if (mapButton->isChecked())
            {
                armed = m;
                Q_EMIT message (tr ("%1 is armed: click a knob in the rays to map it").arg (macroViews[(size_t) m].name->toolTip()));
                sync();
            }
            setFocus (Qt::MouseFocusReason);
        });
        connect (v.column, &QWidget::customContextMenuRequested, this, [this, m] (const QPoint& pos)
        {
            macroMenu (m, macroViews[(size_t) m].column->mapToGlobal (pos));
        });
        macroViews.push_back (v);
    }
    row->addStretch (1);
}

//==============================================================================
bool PrismEditor::follow()
{
    if (engine.prismInfo (ChainPath { slot, {} }).uid != uid)
    {
        int found = -1;
        for (int i = 0; i < AudioEngine::numMasterSlots && found < 0; ++i)
            if (engine.masterSlot (i).prism && engine.prismInfo (ChainPath { i, {} }).uid == uid)
                found = i;
        if (found < 0)
            return false;
        slot = found;
    }

    // A Prism inside that is gone (or moved) takes the editor back out to one that is there.
    for (size_t level = 0; level < steps.size(); ++level)
    {
        const ChainPath p { slot, { steps.begin(), steps.begin() + (long) level + 1 } };
        if (engine.prismInfo (p).uid != stepUids[level])
        {
            steps.resize (level);
            stepUids.resize (level);
            lastSignature.clear();
            break;
        }
    }
    return true;
}

QString PrismEditor::signature (const PrismInfo& info) const
{
    QString s = QString::number (info.uid) + QLatin1Char (':') + QString::number (info.split) + QLatin1Char (':')
              + QString::number (steps.size());
    for (const auto& r : info.rays)
    {
        s += QLatin1Char ('|') + QString::number (r.heard ? 1 : 0);
        for (const auto& d : r.devices)
            s += QLatin1Char (',') + QString::fromStdString (d.pluginId) + (d.failed ? QStringLiteral ("!") : QString());
    }
    return s;
}

void PrismEditor::rebuild()
{
    const auto info = engine.prismInfo (prismPath());
    lastSignature = signature (info);

    // The way back out, when this Prism is inside another.
    while (auto* item = crumbs->takeAt (0))
    {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    for (size_t level = 0; level < steps.size(); ++level)
    {
        const ChainPath p { slot, { steps.begin(), steps.begin() + (long) level } };
        auto* b = button (QString::fromStdString (engine.prismInfo (p).name), tr ("Back out to this Prism"), header);
        const auto out = (int) (steps.size() - level);
        connect (b, &QToolButton::clicked, this, [this, out] { goOut (out); });
        crumbs->addWidget (b);
        crumbs->addWidget (small (QStringLiteral ("›"), header, true));
    }

    if (! nameEdit->hasFocus())
        nameEdit->setText (QString::fromStdString (info.name));
    {
        const QSignalBlocker blocker (splitBox);
        splitBox->setCurrentIndex (info.split);
    }

    // Old rows go; the new ones are built from the Prism as it is now.
    for (auto& v : rayViews)
        v.frame->deleteLater();
    rayViews.clear();
    while (auto* item = raysLayout->takeAt (0))
        delete item;
    for (int r = 0; r < (int) info.rays.size(); ++r)
        buildRay (r, info.rays[(size_t) r]);
    raysLayout->addStretch (1);

    ray = std::clamp (ray, 0, std::max (0, (int) rayViews.size() - 1));
    if (! rayViews.empty())
        device = std::clamp (device, 0, (int) rayViews[(size_t) ray].cards.size() - 1);
    sync();
    showSelection();

    // Grows (upwards, away from the chain) to show every ray, as far as the window allows.
    if (isVisible() && ! closing && parentWidget() != nullptr)
    {
        const auto h = wantedHeight();
        if (h != height())
            setGeometry (x(), std::max (12, geometry().bottom() + 1 - h), width(), h);
    }
}

int PrismEditor::wantedHeight() const
{
    const auto rays = std::max (1, (int) rayViews.size());
    const auto room = parentWidget() != nullptr ? parentWidget()->height() - 24 : 600;
    return std::clamp (176 + rays * 158, std::min (300, room), std::max (300, (int) (room * 0.85)));
}

void PrismEditor::buildRay (int r, const PrismRayInfo& info)
{
    RayView v;
    v.frame = new QFrame (raysHost);
    v.frame->setAcceptDrops (true);
    v.frame->installEventFilter (this);
    v.frame->setProperty ("ray", r);
    v.frame->setFrameShape (QFrame::NoFrame);
    auto* row = new QHBoxLayout (v.frame);
    row->setContentsMargins (0, 2, 0, 2);
    row->setSpacing (4);

    // The ray's own controls, at its start.
    auto* head = new QWidget (v.frame);
    head->setFixedWidth (132);
    auto* headLayout = new QVBoxLayout (head);
    headLayout->setContentsMargins (4, 2, 4, 2);
    headLayout->setSpacing (2);
    auto* nameRow = new QHBoxLayout;
    auto* swatch = new QLabel (head);
    swatch->setFixedSize (10, 10);
    swatch->setStyleSheet (QStringLiteral ("background: %1; border-radius: 5px;").arg (rayColour (r).name()));
    v.name = new QLabel (head);
    v.name->setFont ([&] { auto f = v.name->font(); f.setBold (true); return f; }());
    v.name->setToolTip (tr ("Double-click to rename"));
    v.name->setProperty ("rayName", r);
    v.name->installEventFilter (this);
    nameRow->addWidget (swatch);
    nameRow->addWidget (v.name, 1);
    headLayout->addLayout (nameRow);

    auto* gainRow = new QHBoxLayout;
    v.gain = new Potentiometer (head);
    v.gain->setFixedSize (34, 34);
    v.gain->setRange (-120, 24);   // half-dB steps, -60 dB (off) to +12 dB
    v.gain->setOrigin (0);
    v.gain->setFocusPolicy (Qt::NoFocus);
    v.gain->setAccessibleName (tr ("Gain of ray %1").arg (r + 1));
    v.gainText = small ({}, head, true);
    gainRow->addWidget (v.gain);
    gainRow->addWidget (v.gainText, 1);
    headLayout->addLayout (gainRow);
    connect (v.gain, &QDial::valueChanged, this, [this, r] (int position)
    {
        if (rayViews[(size_t) r].gain->isSliderDown())
            engine.prismSetRayGain (prismPath(), r, position * 0.5);
    });

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing (0);
    v.mute = button (tr ("M"), tr ("Mute this ray (M)"), head, true);
    v.solo = button (tr ("S"), tr ("Solo this ray while listening (S)"), head, true);
    auto* remove = button (QStringLiteral ("×"), tr ("Remove this ray and its plugins"), head);
    buttons->addWidget (v.mute);
    buttons->addWidget (v.solo);
    buttons->addStretch (1);
    buttons->addWidget (remove);
    headLayout->addLayout (buttons);
    connect (v.mute, &QToolButton::clicked, this, [this, r] (bool on) { engine.prismSetRayMuted (prismPath(), r, on); sync(); });
    connect (v.solo, &QToolButton::clicked, this, [this, r] (bool on) { engine.prismSetRaySoloed (prismPath(), r, on); sync(); });
    connect (remove, &QToolButton::clicked, this, [this, r]
    {
        if (rayViews.size() <= 1)
        {
            Q_EMIT message (tr ("A Prism keeps at least one ray"));
            return;
        }
        engine.prismRemoveRay (prismPath(), r);
        rebuild();
        Q_EMIT chainChanged();
    });

    row->addWidget (head);
    v.level = new LevelBar (v.frame);
    v.level->setToolTip (tr ("Level of this ray as it is joined"));
    row->addWidget (v.level);

    v.cardsLayout = new QHBoxLayout;
    v.cardsLayout->setSpacing (4);
    for (int d = 0; d <= (int) info.devices.size(); ++d)
    {
        const auto isAdd = d == (int) info.devices.size();
        auto* card = new DeviceCard (engine, isAdd ? DeviceCard::Kind::add : DeviceCard::Kind::plugin,
                                     isAdd ? ChainPath {} : prismPath().inside (r, d), v.frame);
        v.cards.push_back (card);
        v.cardsLayout->addWidget (card);
        if (! isAdd)
        {
            auto* arrow = new QLabel (QStringLiteral ("›"), v.frame);
            arrow->setForegroundRole (QPalette::PlaceholderText);
            v.cardsLayout->addWidget (arrow);
        }

        connect (card, &DeviceCard::activated, this, [this, r, d] (int knob)
        {
            select (r, d);
            if (knob >= 0 && mapButton->isChecked())
                deviceKnobClicked (r, d, knob);
            setFocus (Qt::MouseFocusReason);
        });
        connect (card, &DeviceCard::changed, this, [this] { sync(); Q_EMIT chainChanged(); });
        connect (card, &DeviceCard::message, this, &PrismEditor::message);
        connect (card, &DeviceCard::valueEditFinished, this, [this] { setFocus (Qt::OtherFocusReason); });
        connect (card, &DeviceCard::editRequested, this, [this, r, d, card]
        {
            if (card->prism())
                enter (r, d);
            else
            {
                openParameterWindow (engine, card->path(), window());
            }
        });
        connect (card, &DeviceCard::windowRequested, this, [this, r, d] { openDevice (r, d); });
        connect (card, &DeviceCard::addRequested, this, [this, r, d] { addPlugin (r, d); });
    }
    row->addLayout (v.cardsLayout);
    row->addStretch (1);
    raysLayout->addWidget (v.frame);
    rayViews.push_back (v);
}

void PrismEditor::sync()
{
    const auto path = prismPath();
    const auto info = engine.prismInfo (path);
    if (! info.valid)
        return;

    for (int m = 0; m < PrismInfo::numMacros && m < (int) macroViews.size(); ++m)
    {
        auto& v = macroViews[(size_t) m];
        const auto& macro = info.macros[(size_t) m];
        const auto name = QString::fromStdString (macro.name);
        if (v.name->toolTip() != name)
        {
            v.name->setText (QFontMetrics (v.name->font()).elidedText (name, Qt::ElideRight, v.name->width()));
            v.name->setToolTip (name);
            v.pot->setAccessibleName (name);
        }
        auto f = v.name->font();
        f.setBold (armed == m);
        v.name->setFont (f);
        v.name->setForegroundRole (armed == m ? QPalette::Link : QPalette::WindowText);
        if (! v.pot->isSliderDown())
        {
            const QSignalBlocker blocker (v.pot);
            v.pot->setValue ((int) std::lround (macro.value * resolution));
        }
        v.value->setText (QString::fromStdString (macro.valueText)
                          + (macro.mappings.empty() ? QString() : tr ("  ·%1").arg (macro.mappings.size())));
        QStringList targets;
        for (const auto& map : macro.mappings)
            targets << tr ("%1: %2 (%3 to %4)").arg (QString::fromStdString (map.deviceName), QString::fromStdString (map.parameterName),
                                                    percent (map.low), percent (map.high));
        v.value->setToolTip (targets.isEmpty() ? tr ("Nothing mapped yet: press Map, turn this macro, then click a knob")
                                               : targets.join (QLatin1Char ('\n')));
        const auto learning = engine.prismLearning (path, m);
        v.midi->setText (learning ? tr ("move a control")
                         : macro.controller >= 0 ? (macro.channel > 0 ? tr ("CC %1 ch %2").arg (macro.controller).arg (macro.channel)
                                                                       : tr ("CC %1").arg (macro.controller))
                                                 : QStringLiteral ("–"));
        v.midi->setForegroundRole (learning ? QPalette::Link : QPalette::PlaceholderText);
    }

    const auto heard = (int) std::count_if (info.rays.begin(), info.rays.end(), [] (const PrismRayInfo& r) { return r.heard; });
    for (int i = 0; i < 3; ++i)
    {
        const auto shown = info.split == PrismInfo::bands && i + 1 < heard;
        crossoverPots[(size_t) i]->setVisible (shown);
        crossoverLabels[(size_t) i]->setVisible (shown);
        if (! shown)
            continue;
        const auto p = engine.chainParameter (path, PrismInfo::numMacros + 1 + i);
        if (! crossoverPots[(size_t) i]->isSliderDown())
        {
            const QSignalBlocker blocker (crossoverPots[(size_t) i]);
            crossoverPots[(size_t) i]->setValue ((int) std::lround (p.value * resolution));
        }
        crossoverLabels[(size_t) i]->setText (QString::fromStdString (p.valueText));
    }

    for (int r = 0; r < (int) rayViews.size() && r < (int) info.rays.size(); ++r)
    {
        auto& v = rayViews[(size_t) r];
        const auto& ri = info.rays[(size_t) r];
        auto name = QString::fromStdString (ri.name);
        v.name->setText (QFontMetrics (v.name->font()).elidedText (name, Qt::ElideRight, 100));
        v.name->setForegroundRole (ri.heard ? QPalette::WindowText : QPalette::PlaceholderText);
        v.frame->setToolTip (ri.heard ? QString() : tr ("This split does not feed this ray, so it is not heard"));
        if (! v.gain->isSliderDown())
        {
            const QSignalBlocker blocker (v.gain);
            v.gain->setValue ((int) std::lround (ri.gainDb * 2.0));
        }
        v.gainText->setText (ri.gainDb <= -60.0 ? tr ("Off") : QString::asprintf ("%+.1f dB", ri.gainDb));
        v.mute->setChecked (ri.muted);
        v.solo->setChecked (ri.soloed);
        v.level->push (engine.prismTakeRayPeak (path, r));
        for (auto* c : v.cards)
            c->sync();
    }
}

//==============================================================================
DeviceCard* PrismEditor::currentCard() const
{
    if (ray < 0 || ray >= (int) rayViews.size())
        return nullptr;
    const auto& cards = rayViews[(size_t) ray].cards;
    return device >= 0 && device < (int) cards.size() ? cards[(size_t) device] : nullptr;
}

std::pair<int, int> PrismEditor::range() const
{
    if (anchor < 0)
        return { device, device };
    return { std::min (anchor, device), std::max (anchor, device) };
}

void PrismEditor::select (int r, int d)
{
    if (rayViews.empty())
        return;
    anchor = -1;
    ray = std::clamp (r, 0, (int) rayViews.size() - 1);
    device = std::clamp (d, 0, (int) rayViews[(size_t) ray].cards.size() - 1);
    showSelection();
    if (auto* c = currentCard())
        raysScroll->ensureWidgetVisible (c, 8, 8);
}

void PrismEditor::showSelection()
{
    const auto [first, last] = range();
    for (int r = 0; r < (int) rayViews.size(); ++r)
        for (int d = 0; d < (int) rayViews[(size_t) r].cards.size(); ++d)
        {
            auto* c = rayViews[(size_t) r].cards[(size_t) d];
            c->setSelected (r == ray && d == device, hasFocus(), -1);
            c->setInRange (r == ray && anchor >= 0 && first != last && d >= first && d <= last);
        }
}

//==============================================================================
void PrismEditor::enter (int r, int d)
{
    const auto inner = prismPath().inside (r, d);
    const auto info = engine.prismInfo (inner);
    if (! info.valid)
        return;
    steps.push_back ({ r, d });
    stepUids.push_back (info.uid);
    ray = device = 0;
    armed = -1;
    mapButton->setChecked (false);
    rebuild();
    Q_EMIT message (tr ("In %1: Esc goes back out").arg (QString::fromStdString (info.name)));
}

void PrismEditor::goOut (int levels)
{
    if (levels <= 0)
        return;
    if (levels > (int) steps.size())
    {
        dock();
        return;
    }
    // Lands on the Prism it came out of.
    const auto [r, d] = steps[steps.size() - (size_t) levels];
    steps.resize (steps.size() - (size_t) levels);
    stepUids.resize (steps.size());
    armed = -1;
    mapButton->setChecked (false);
    rebuild();
    select (r, d);
}

void PrismEditor::addPlugin (int r, int index)
{
    if (! pickPlugin)
        return;
    const auto id = pickPlugin();
    setFocus (Qt::OtherFocusReason);
    if (id.empty())
        return;

    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto error = engine.prismAddDevice (prismPath(), r, index, id);
    QApplication::restoreOverrideCursor();
    if (! error.empty())
    {
        Q_EMIT message (QString::fromStdString (error));
        return;
    }
    rebuild();
    select (r, index);
    Q_EMIT chainChanged();
    const auto path = prismPath().inside (r, index);
    if (engine.canShowChainWindow (path))
        engine.showChainWindow (path);
}

void PrismEditor::openDevice (int r, int d)
{
    const auto path = prismPath().inside (r, d);
    const auto info = engine.chainDevice (path);
    if (info.empty)
        return;
    if (info.prism && ! info.failed)
        enter (r, d);
    else if (info.failed)
    {
        const auto error = engine.reloadChainDevice (path);
        Q_EMIT message (error.empty() ? tr ("%1 loaded again with its last settings").arg (QString::fromStdString (info.name))
                                      : QString::fromStdString (error));
        rebuild();
    }
    else if (engine.canShowChainWindow (path))
        engine.showChainWindow (path);
    else
    {
        openParameterWindow (engine, path, window());
    }
}

void PrismEditor::removeCurrent()
{
    auto* card = currentCard();
    if (card == nullptr || card->kind() != DeviceCard::Kind::plugin)
        return;
    const auto name = QString::fromStdString (engine.chainDevice (card->path()).name);
    engine.removeChainDevice (card->path());
    rebuild();
    select (ray, device);
    Q_EMIT chainChanged();
    Q_EMIT message (tr ("Removed %1").arg (name));
}

void PrismEditor::moveCurrent (int rayDelta, int deviceDelta)
{
    auto* card = currentCard();
    if (card == nullptr || card->kind() != DeviceCard::Kind::plugin)
        return;
    const auto devices = [this] (int r) { return (int) rayViews[(size_t) r].cards.size() - 1; };
    int toRay = ray, to = device;
    if (rayDelta != 0)
    {
        toRay = ray + rayDelta;
        if (toRay < 0 || toRay >= (int) rayViews.size())
            return;
        to = std::min (device, devices (toRay));
    }
    else
    {
        to = device + deviceDelta;
        if (to < 0 || to >= devices (ray))
            return;
    }
    engine.prismMoveDevice (prismPath(), ray, device, toRay, to);
    rebuild();
    select (toRay, to);
    Q_EMIT chainChanged();
}

void PrismEditor::groupSelection()
{
    const auto [first, last] = range();
    const auto devices = ray < (int) rayViews.size() ? (int) rayViews[(size_t) ray].cards.size() - 1 : 0;
    if (first >= devices)
    {
        Q_EMIT message (tr ("Pick a device (Shift+Left/Right for several), then Ctrl+G groups them into a Prism"));
        return;
    }
    const auto error = engine.prismGroup (prismPath(), ray, first, std::min (last, devices - 1));
    if (! error.empty())
    {
        Q_EMIT message (QString::fromStdString (error));
        return;
    }
    rebuild();
    select (ray, first);
    Q_EMIT chainChanged();
    Q_EMIT message (tr ("Grouped into a Prism inside this one; Enter opens it"));
}

void PrismEditor::ungroupCurrent()
{
    auto* card = currentCard();
    if (card == nullptr || ! card->prism())
    {
        Q_EMIT message (tr ("Ctrl+Shift+G ungroups a Prism; pick one first"));
        return;
    }
    const auto error = engine.prismUngroup (card->path());
    rebuild();
    Q_EMIT chainChanged();
    Q_EMIT message (error.empty() ? tr ("Prism ungrouped") : QString::fromStdString (error));
}

void PrismEditor::addRay()
{
    const auto r = engine.prismAddRay (prismPath());
    if (r < 0)
    {
        Q_EMIT message (tr ("A Prism has at most %1 rays").arg (PrismInfo::maxRays));
        return;
    }
    rebuild();
    select (r, 0);
    Q_EMIT chainChanged();
    if (engine.prismInfo (prismPath()).split == PrismInfo::parallel && r > 0)
        Q_EMIT message (tr ("Rays in parallel are added together: an empty ray is the dry sound"));
}

void PrismEditor::deviceKnobClicked (int r, int d, int knob)
{
    if (armed < 0)
    {
        Q_EMIT message (tr ("Map: first turn or click the macro to map to"));
        return;
    }
    auto* card = rayViews[(size_t) r].cards[(size_t) d];
    const auto parameter = card->parameterOf (knob);
    if (parameter < 0)
        return;
    const auto error = engine.prismMap (prismPath(), armed, r, d, parameter);
    const auto p = engine.chainParameter (card->path(), parameter);
    Q_EMIT message (error.empty() ? tr ("%1 now turns %2 of %3 (right-click the macro to set its range)")
                                        .arg (macroViews[(size_t) armed].name->toolTip(), QString::fromStdString (p.name),
                                              QString::fromStdString (engine.chainDevice (card->path()).name))
                                  : QString::fromStdString (error));
    sync();
}

void PrismEditor::renameMacro (int m)
{
    bool ok = false;
    const auto current = macroViews[(size_t) m].name->toolTip();
    const auto text = QInputDialog::getText (this, tr ("Rename Macro"), tr ("Name:"), QLineEdit::Normal, current, &ok);
    if (ok)
    {
        engine.prismRenameMacro (prismPath(), m, text.toStdString());
        sync();
        Q_EMIT chainChanged();
    }
    setFocus (Qt::OtherFocusReason);
}

void PrismEditor::renameRay (int r)
{
    const auto info = engine.prismInfo (prismPath());
    if (r < 0 || r >= (int) info.rays.size())
        return;
    bool ok = false;
    const auto text = QInputDialog::getText (this, tr ("Rename Ray"), tr ("Name (empty: named by the split):"), QLineEdit::Normal,
                                             info.rays[(size_t) r].named ? QString::fromStdString (info.rays[(size_t) r].name) : QString(), &ok);
    if (ok)
    {
        engine.prismRenameRay (prismPath(), r, text.toStdString());
        sync();
    }
    setFocus (Qt::OtherFocusReason);
}

void PrismEditor::macroMenu (int m, const QPoint& globalPos)
{
    const auto path = prismPath();
    const auto info = engine.prismInfo (path);
    const auto& macro = info.macros[(size_t) m];

    QMenu menu (this);
    menu.addAction (tr ("Rename..."), this, [this, m] { renameMacro (m); });
    menu.addAction (tr ("Map a Knob"), this, [this, m]
    {
        mapButton->setChecked (true);
        armed = m;
        sync();
        Q_EMIT message (tr ("Click a knob in the rays to map it to %1").arg (macroViews[(size_t) m].name->toolTip()));
    });
    menu.addSeparator();
    if (engine.prismLearning (path, m))
        menu.addAction (tr ("Stop Waiting for MIDI"), this, [this, path] { engine.prismLearnController (path, -1); sync(); });
    else
        menu.addAction (tr ("Learn MIDI Controller"), this, [this, path, m]
        {
            engine.prismLearnController (path, m);
            Q_EMIT message (tr ("Move a knob or fader on your MIDI controller (open it under Sample > MIDI Inputs)"));
            sync();
        });
    if (macro.controller >= 0)
        menu.addAction (tr ("Forget CC %1").arg (macro.controller), this, [this, path, m] { engine.prismForgetController (path, m); sync(); });

    menu.addSeparator();
    if (macro.mappings.empty())
        menu.addAction (tr ("Nothing mapped yet"))->setEnabled (false);
    for (int i = 0; i < (int) macro.mappings.size(); ++i)
    {
        const auto& map = macro.mappings[(size_t) i];
        auto* sub = menu.addMenu (tr ("%1: %2 (%3 to %4)").arg (QString::fromStdString (map.deviceName), QString::fromStdString (map.parameterName),
                                                                percent (map.low), percent (map.high)));
        sub->addAction (tr ("Range..."), this, [this, m, i] { editRange (m, i); });
        sub->addAction (tr ("Reverse"), this, [this, path, m, i, map]
        {
            engine.prismSetMappingRange (path, m, i, map.high, map.low);
            sync();
        });
        sub->addAction (tr ("Remove"), this, [this, path, m, i] { engine.prismUnmap (path, m, i); sync(); });
    }
    menu.exec (globalPos);
    setFocus (Qt::OtherFocusReason);
}

void PrismEditor::editRange (int m, int mapping)
{
    const auto path = prismPath();
    const auto info = engine.prismInfo (path);
    if (mapping < 0 || mapping >= (int) info.macros[(size_t) m].mappings.size())
        return;
    const auto map = info.macros[(size_t) m].mappings[(size_t) mapping];

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Range of %1").arg (QString::fromStdString (map.parameterName)));
    auto* form = new QFormLayout (&dialog);
    auto spin = [&] (float v)
    {
        auto* box = new QDoubleSpinBox (&dialog);
        box->setRange (0.0, 100.0);
        box->setDecimals (1);
        box->setSuffix (QStringLiteral (" %"));
        box->setValue (v * 100.0);
        return box;
    };
    auto* low = spin (map.low);
    auto* high = spin (map.high);
    form->addRow (tr ("With the macro at its start:"), low);
    form->addRow (tr ("With the macro at its end:"), high);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        engine.prismSetMappingRange (path, m, mapping, (float) (low->value() / 100.0), (float) (high->value() / 100.0));
    sync();
}

//==============================================================================
QRect PrismEditor::cardRect() const
{
    QWidget* card = findCard ? findCard (uid) : nullptr;
    if (card == nullptr)
        card = dockCard;
    if (card == nullptr || parentWidget() == nullptr || ! card->isVisible())
        return { geometry().center(), QSize (1, 1) };
    return { card->mapTo (parentWidget(), QPoint (0, 0)), card->size() };
}

void PrismEditor::animateTo (const QRect& target, std::function<void()> then)
{
    auto* animation = new QPropertyAnimation (this, "geometry", this);
    animation->setDuration (180);
    animation->setStartValue (geometry());
    animation->setEndValue (target);
    animation->setEasingCurve (closing ? QEasingCurve::InCubic : QEasingCurve::OutCubic);
    connect (animation, &QPropertyAnimation::finished, this, [then = std::move (then)] { if (then) then(); });
    animation->start (QAbstractAnimation::DeleteWhenStopped);
}

void PrismEditor::popOut (QWidget* card)
{
    dockCard = card;
    auto* parent = parentWidget();
    const auto from = cardRect();
    const auto w = std::clamp (900, 420, std::max (420, parent->width() - 24));
    const auto h = wantedHeight();
    // Rises from the card: above it when there is room, kept inside the window.
    const auto x = std::clamp (from.left(), 12, std::max (12, parent->width() - w - 12));
    const auto y = std::clamp (from.top() - h - 10, 12, std::max (12, parent->height() - h - 12));
    QRect to (x, y, w, h);
    // Where it was left last time, floating over whatever is there (owner, 2026-10-25), as
    // long as that is still inside the window.
    if (const auto last = appSettings().value (QStringLiteral ("prism/place")).toRect();
        last.isValid() && last.width() >= 420 && parent->rect().adjusted (-40, 0, 40, 0).contains (last.topLeft())
        && last.right() <= parent->width() + 40 && last.top() + 40 <= parent->height())
        to = QRect (last.topLeft(), QSize (std::min (last.width(), parent->width()), std::max (h, std::min (last.height(), parent->height()))));

    setGeometry (from);
    show();
    raise();
    animateTo (to, {});
    setFocus (Qt::OtherFocusReason);
}

void PrismEditor::dock()
{
    if (closing)
        return;
    closing = true;
    timer.stop();
    engine.prismLearnController (prismPath(), -1);
    appSettings().setValue (QStringLiteral ("prism/place"), geometry());   // it opens there next time
    animateTo (cardRect(), [this]
    {
        hide();
        Q_EMIT docked (true);
        deleteLater();
    });
}

void PrismEditor::dockNow()
{
    if (closing && ! isVisible())
        return;
    closing = true;
    timer.stop();
    if (isVisible())
        appSettings().setValue (QStringLiteral ("prism/place"), geometry());
    hide();
    Q_EMIT docked (false);
    deleteLater();
}

//==============================================================================
void PrismEditor::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto r = QRectF (rect()).adjusted (0.5, 0.5, -0.5, -0.5);
    g.setPen (QPen (palette().color (QPalette::Highlight), 1.0));
    g.setBrush (palette().color (QPalette::Window));
    g.drawRoundedRect (r, 10, 10);

    // A band of spectrum along the top edge.
    QLinearGradient band (r.topLeft(), r.topRight());
    for (int i = 0; i <= 6; ++i)
        band.setColorAt (i / 6.0, rayColour (i));
    g.setPen (Qt::NoPen);
    g.setBrush (band);
    g.drawRoundedRect (QRectF (r.left() + 10, r.top() + 1, r.width() - 20, 3), 1.5, 1.5);
}

void PrismEditor::mousePressEvent (QMouseEvent* e)
{
    setFocus (Qt::MouseFocusReason);
    QFrame::mousePressEvent (e);
}

bool PrismEditor::eventFilter (QObject* watched, QEvent* e)
{
    // The header moves the editor.
    if (watched == header)
    {
        auto* m = static_cast<QMouseEvent*> (e);
        if (e->type() == QEvent::MouseButtonPress && m->button() == Qt::LeftButton)
        {
            dragging = true;
            dragFrom = m->globalPosition().toPoint() - pos();
            header->setCursor (Qt::ClosedHandCursor);
            setFocus (Qt::MouseFocusReason);
            return true;
        }
        if (e->type() == QEvent::MouseMove && dragging)
        {
            auto next = m->globalPosition().toPoint() - dragFrom;
            if (auto* parent = parentWidget())
            {
                next.setX (std::clamp (next.x(), 0, std::max (0, parent->width() - width())));
                next.setY (std::clamp (next.y(), 0, std::max (0, parent->height() - 40)));
            }
            move (next);
            return true;
        }
        if (e->type() == QEvent::MouseButtonRelease && dragging)
        {
            dragging = false;
            header->setCursor (Qt::OpenHandCursor);
            return true;
        }
        return false;
    }

    if (e->type() == QEvent::MouseButtonDblClick)
    {
        if (const auto m = watched->property ("macro"); m.isValid())
        {
            renameMacro (m.toInt());
            return true;
        }
        if (const auto r = watched->property ("rayName"); r.isValid())
        {
            renameRay (r.toInt());
            return true;
        }
    }

    // Dragging a device from one ray to another, or along its own.
    if (const auto target = watched->property ("ray"); target.isValid())
    {
        if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove)
        {
            auto* d = static_cast<QDragMoveEvent*> (e);
            if (d->mimeData()->hasFormat (DeviceCard::prismDeviceMime()))
            {
                d->acceptProposedAction();
                return true;
            }
        }
        if (e->type() == QEvent::Drop)
        {
            auto* d = static_cast<QDropEvent*> (e);
            const auto parts = QString::fromLatin1 (d->mimeData()->data (DeviceCard::prismDeviceMime())).split (QLatin1Char (','));
            if (parts.size() != 2)
                return false;
            const auto fromRay = parts[0].toInt(), from = parts[1].toInt();
            const auto toRay = target.toInt();
            const auto& cards = rayViews[(size_t) toRay].cards;
            auto to = (int) cards.size() - 1;   // after the last device
            for (int i = 0; i + 1 < (int) cards.size(); ++i)
                if (d->position().x() < cards[(size_t) i]->geometry().center().x())
                {
                    to = i;
                    break;
                }
            if (fromRay == toRay && to > from)
                --to;
            d->acceptProposedAction();
            if (fromRay != toRay || to != from)
            {
                engine.prismMoveDevice (prismPath(), fromRay, from, toRay, to);
                rebuild();
                select (toRay, to);
                Q_EMIT chainChanged();
            }
            setFocus (Qt::MouseFocusReason);
            return true;
        }
    }
    return QFrame::eventFilter (watched, e);
}

bool PrismEditor::handles (const QKeyEvent* k)
{
    const auto mods = k->modifiers() & ~Qt::KeypadModifier;
    const auto key = k->key();
    if (mods == Qt::ControlModifier)
        return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_G || key == Qt::Key_T;
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier))
        return key == Qt::Key_G;
    if (mods == Qt::ShiftModifier)
        return key == Qt::Key_Left || key == Qt::Key_Right;
    if (mods != Qt::NoModifier)
        return false;
    switch (key)
    {
        case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
        case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Insert: case Qt::Key_Delete: case Qt::Key_Backspace:
        case Qt::Key_B: case Qt::Key_E: case Qt::Key_M: case Qt::Key_S: case Qt::Key_F2: case Qt::Key_Escape:
            return true;
        default:
            return false;
    }
}

bool PrismEditor::event (QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride && hasFocus() && handles (static_cast<QKeyEvent*> (e)))
    {
        e->accept();
        return true;
    }
    if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut)
        showSelection();
    return QFrame::event (e);
}

void PrismEditor::keyPressEvent (QKeyEvent* k)
{
    if (! handles (k) || rayViews.empty())
    {
        QFrame::keyPressEvent (k);
        return;
    }

    const auto ctrl = (k->modifiers() & Qt::ControlModifier) != 0;
    const auto shift = (k->modifiers() & Qt::ShiftModifier) != 0;
    auto* card = currentCard();
    const auto isDevice = card != nullptr && card->kind() == DeviceCard::Kind::plugin;

    switch (k->key())
    {
        case Qt::Key_Escape:
            goOut (1);
            break;

        case Qt::Key_Left:
        case Qt::Key_Right:
        {
            const auto delta = k->key() == Qt::Key_Left ? -1 : 1;
            if (ctrl)
                moveCurrent (0, delta);
            else if (shift)
            {
                const auto next = device + delta;
                if (! isDevice || next < 0 || next >= (int) rayViews[(size_t) ray].cards.size() - 1)
                    break;
                const auto keep = anchor < 0 ? device : anchor;
                select (ray, next);
                anchor = keep;
                showSelection();
            }
            else
                select (ray, device + delta);
            break;
        }

        case Qt::Key_Up:
        case Qt::Key_Down:
            if (ctrl)
                moveCurrent (k->key() == Qt::Key_Up ? -1 : 1, 0);
            else
                select (ray + (k->key() == Qt::Key_Up ? -1 : 1), device);
            break;

        case Qt::Key_G:
            if (shift)
                ungroupCurrent();
            else
                groupSelection();
            break;

        case Qt::Key_T:
            addRay();
            break;

        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_E:
            if (isDevice)
                openDevice (ray, device);
            else if (card != nullptr)
                addPlugin (ray, device);
            break;

        case Qt::Key_Insert:
            addPlugin (ray, isDevice ? device + 1 : device);
            break;

        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            removeCurrent();
            break;

        case Qt::Key_B:
            if (isDevice)
            {
                const auto info = engine.chainDevice (card->path());
                engine.setChainBypassed (card->path(), ! info.bypassed);
                Q_EMIT message (info.bypassed ? tr ("%1 on").arg (QString::fromStdString (info.name))
                                              : tr ("%1 bypassed").arg (QString::fromStdString (info.name)));
                sync();
            }
            break;

        case Qt::Key_M:
        case Qt::Key_S:
        {
            const auto info = engine.prismInfo (prismPath());
            if (ray >= (int) info.rays.size())
                break;
            const auto& r = info.rays[(size_t) ray];
            if (k->key() == Qt::Key_M)
                engine.prismSetRayMuted (prismPath(), ray, ! r.muted);
            else
                engine.prismSetRaySoloed (prismPath(), ray, ! r.soloed);
            sync();
            break;
        }

        case Qt::Key_F2:
            nameEdit->setFocus (Qt::ShortcutFocusReason);
            nameEdit->selectAll();
            break;

        default:
            break;
    }
}
