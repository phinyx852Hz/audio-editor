// SPDX-License-Identifier: AGPL-3.0-only
#include "ChainRow.h"
#include "MasterPanel.h"
#include "PrismEditor.h"
#include "Potentiometer.h"
#include "PhiMark.h"
#include "Theme.h"

#include <QApplication>
#include <QDial>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    const QString slotMime = QStringLiteral ("application/x-phi-chain-slot");
    constexpr int dialResolution = 1000;
    constexpr int maxKnobs = 4;

    QToolButton* smallButton (const QString& text, const QString& tip, QWidget* parent, bool checkable = false)
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

    QString elided (const QString& text, const QFont& font, int width)
    {
        return QFontMetrics (font).elidedText (text, Qt::ElideRight, width);
    }
}

//==============================================================================
LevelBar::LevelBar (QWidget* parent) : QWidget (parent)
{
    setFixedWidth (6);
    setMinimumHeight (40);
    setToolTip (tr ("Output level of this plugin"));
}

void LevelBar::push (float linearPeak)
{
    const auto db = linearPeak > 0.0f ? 20.0f * std::log10 (linearPeak) : -100.0f;
    const auto next = std::max (db, shownDb - 3.0f);  // falls about 30 dB per second
    if (std::abs (next - shownDb) > 0.05f)
    {
        shownDb = next;
        update();
    }
}

void LevelBar::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto& theme = Theme::current();
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("meterBackground"))));

    const auto fraction = std::clamp ((shownDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    const auto h = (int) std::lround (fraction * height());
    const auto key = shownDb > -0.1f ? QStringLiteral ("meterHigh") : shownDb > -9.0f ? QStringLiteral ("meterMid")
                                                                                       : QStringLiteral ("meterLow");
    g.fillRect (QRect (0, height() - h, width(), h), theme.color (key));
}

//==============================================================================
DeviceCard::DeviceCard (AudioEngine& e, Kind k, ChainPath where, QWidget* parent)
    : QFrame (parent), engine (e), cardKind (k), place (std::move (where))
{
    setFocusPolicy (Qt::NoFocus);
    const auto info = k == Kind::plugin ? engine.chainDevice (place) : MasterSlotInfo {};
    isPrism = info.prism && ! info.failed;
    setFixedWidth (k == Kind::add ? 110 : k == Kind::master ? 186 : isPrism ? 440 : 236);
    setMinimumHeight (150);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (8, 6, 8, 6);
    layout->setSpacing (3);

    if (k == Kind::add)
    {
        auto* add = smallButton (tr ("+\nAdd plugin"), tr ("Add a plugin (Insert)"), this);
        add->setToolButtonStyle (Qt::ToolButtonTextOnly);
        add->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
        layout->addWidget (add);
        connect (add, &QToolButton::clicked, this, &DeviceCard::addRequested);
        watch (add);
        return;
    }

    auto* header = new QHBoxLayout;
    header->setSpacing (2);
    power = smallButton (tr ("On"), k == Kind::master ? tr ("Master Section on or bypassed (B)") : tr ("Plugin on or bypassed (B)"), this, true);
    title = new QLabel (this);
    title->setFont ([&] { auto f = font(); f.setBold (true); return f; }());
    header->addWidget (power);
    const auto phiPlugin = k == Kind::plugin && phimark::isPhiPlugin (info.pluginId);
    if (phiPlugin)
        header->addWidget (new PhiMark (power->sizeHint().height(), this));
    header->addWidget (title, 1);

    if (k == Kind::plugin && place.topLevel())
    {
        solo = smallButton (tr ("S"), tr ("Solo: hear only this plugin while playing (S)"), this, true);
        header->addWidget (solo);
    }

    layout->addLayout (header);

    detail = new QLabel (this);
    detail->setForegroundRole (QPalette::PlaceholderText);
    layout->addWidget (detail);

    auto* body = new QHBoxLayout;
    body->setSpacing (4);
    auto* knobGrid = new QHBoxLayout;
    knobGrid->setSpacing (2);
    body->addLayout (knobGrid, 1);
    layout->addLayout (body, 1);

    if (k == Kind::plugin && isPrism)
    {
        // A Prism's eight macros, and a strip showing its rays.
        title->setText (elided (QString::fromStdString (info.name), title->font(), 300));
        title->setToolTip (QString::fromStdString (info.name));
        const auto prism = engine.prismInfo (place);
        for (int m = 0; m < PrismInfo::numMacros; ++m)
            addKnob (m, QString::fromStdString (prism.macros[(size_t) m].name), 0);
        rays = new PrismStrip (this);
        layout->insertWidget (2, rays);
    }
    else if (k == Kind::plugin)
    {
        const auto name = QString::fromStdString (info.name);
        title->setText (elided (phiPlugin ? phimark::withoutPhi (name) : name, title->font(), 150));
        title->setToolTip (name);

        // A stock plugin's main controls, the same as its window's footer; otherwise the first
        // few continuous parameters. Switches and the plugin's own bypass are left to Edit.
        const auto params = engine.chainParameters (place);
        for (const auto i : info.mainParameters)
            if (i >= 0 && i < (int) params.size() && (int) knobs.size() < maxKnobs)
                addKnob (i, QString::fromStdString (params[(size_t) i].name), params[(size_t) i].numSteps);
        for (int i = 0; i < (int) params.size() && (int) knobs.size() < maxKnobs && info.mainParameters.empty(); ++i)
        {
            const auto& p = params[(size_t) i];
            const auto name = QString::fromStdString (p.name);
            if (! p.isBoolean && p.numSteps != 2 && ! name.contains (QStringLiteral ("bypass"), Qt::CaseInsensitive))
                addKnob (i, name, p.numSteps);
        }
    }
    else
    {
        title->setText (tr ("Master"));
        addKnob (masterGain, tr ("Gain"), 0);
        // The final dither lives here, after the gain, so it never takes a plugin slot.
        addKnob (ditherDepth, tr ("Dither"), 4);
        addKnob (ditherNoise, tr ("Noise"), 3);
    }

    for (auto& kn : knobs)
    {
        auto* column = new QVBoxLayout;
        column->setSpacing (0);
        column->addWidget (kn.dial, 0, Qt::AlignHCenter);
        column->addWidget (kn.name);
        column->addWidget (kn.value);
        knobGrid->addLayout (column);
    }

    if (knobs.empty())
    {
        auto* none = new QLabel (tr ("No adjustable parameters"), this);
        none->setForegroundRole (QPalette::PlaceholderText);
        none->setWordWrap (true);
        knobGrid->addWidget (none);
    }

    if (k == Kind::plugin)
    {
        level = new LevelBar (this);
        body->addWidget (level);

        auto* footer = new QHBoxLayout;
        auto* edit = smallButton (tr ("Edit..."), tr ("All parameters (Enter)"), this);
        auto* window = windowButton = smallButton (tr ("Window"), tr ("The plugin's own window (E)"), this);
        if (isPrism)
        {
            edit->setText (tr ("Open"));
            edit->setToolTip (tr ("Pop the Prism out to edit its rays; it docks back when you are done (Enter)"));
            window->hide();
        }
        footer->addWidget (edit);
        footer->addWidget (window);
        footer->addStretch (1);
        layout->addLayout (footer);
        connect (edit, &QToolButton::clicked, this, &DeviceCard::editRequested);
        connect (window, &QToolButton::clicked, this, &DeviceCard::windowRequested);
        watch (edit);
        watch (window);

        connect (power, &QToolButton::clicked, this, [this] (bool on)
        {
            engine.setChainBypassed (place, ! on);
            Q_EMIT changed();
        });
        if (solo != nullptr)
        {
            connect (solo, &QToolButton::clicked, this, [this] (bool on)
            {
                engine.setMasterSoloSlot (on ? place.slot : -1);
                Q_EMIT changed();
            });
            watch (solo);
        }
    }
    else
    {
        connect (power, &QToolButton::clicked, this, [this] (bool on)
        {
            engine.setMasterBypassed (! on);
            Q_EMIT changed();
        });
    }

    watch (power);
    watch (title);
    watch (detail);
    sync();
}

void DeviceCard::watch (QWidget* w)
{
    w->installEventFilter (this);
}

void DeviceCard::addKnob (int parameter, const QString& name, int steps)
{
    Knob kn;
    kn.parameter = parameter;
    kn.steps = steps;
    auto* pot = new Potentiometer (this);
    kn.dial = pot;
    const auto labelWidth = isPrism ? 48 : 52;
    kn.dial->setFixedSize (isPrism ? 32 : 40, isPrism ? 32 : 40);
    kn.dial->setFocusPolicy (Qt::NoFocus);
    kn.dial->setWrapping (false);
    kn.dial->setAccessibleName (name);
    if (parameter == masterGain)
    {
        kn.dial->setRange (-120, 48);  // master gain in half-dB steps
        pot->setOrigin (0);            // filled from 0 dB, up or down
    }
    else if (parameter < 0)
        kn.dial->setRange (0, steps - 1);
    else
        kn.dial->setRange (0, steps > 1 ? steps - 1 : dialResolution);

    kn.name = new QLabel (elided (name, font(), labelWidth - 2), this);
    kn.name->setToolTip (name);
    kn.name->setAlignment (Qt::AlignHCenter);
    kn.value = new QLabel (this);
    kn.value->setAlignment (Qt::AlignHCenter);
    kn.value->setForegroundRole (QPalette::PlaceholderText);
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.85);
    kn.name->setFont (small);
    kn.value->setFont (small);
    kn.name->setFixedWidth (labelWidth);
    kn.value->setFixedWidth (labelWidth);

    const auto index = knobs.size();
    connect (kn.dial, &QDial::valueChanged, this, [this, index] (int position)
    {
        if (! syncing)
            knobMoved (knobs[index], position);
    });
    watch (kn.dial);
    watch (kn.name);
    watch (kn.value);
    knobs.push_back (kn);
}

void DeviceCard::knobMoved (const Knob& kn, int position)
{
    static constexpr int depths[] { 0, 16, 20, 24 };
    if (kn.parameter == masterGain)
        engine.setMasterGainDb (position * 0.5);
    else if (kn.parameter == ditherDepth)
        engine.setMasterDither (depths[std::clamp (position, 0, 3)], engine.masterDitherType());
    else if (kn.parameter == ditherNoise)
        engine.setMasterDither (engine.masterDitherBits(), (dsp::Dither) std::clamp (position, 0, 2));
    else
        engine.setChainParameter (place, kn.parameter, (float) position / (float) std::max (1, kn.dial->maximum()));
    sync();
}

void DeviceCard::setKnobFromEngine (Knob& kn)
{
    QString text;
    int position = 0;

    if (kn.parameter == masterGain)
    {
        const auto db = engine.masterGainDb();
        position = (int) std::lround (db * 2.0);
        text = QString::asprintf ("%+.1f dB", db);
    }
    else if (kn.parameter == ditherDepth)
    {
        const auto bits = engine.masterDitherBits();
        position = bits == 16 ? 1 : bits == 20 ? 2 : bits == 24 ? 3 : 0;
        text = bits > 0 ? tr ("%1-bit").arg (bits) : tr ("Off");
    }
    else if (kn.parameter == ditherNoise)
    {
        const auto type = engine.masterDitherType();
        position = (int) type;
        text = type == dsp::Dither::none ? tr ("Round") : type == dsp::Dither::tpdf ? tr ("TPDF") : tr ("Shaped");
        kn.dial->setEnabled (engine.masterDitherBits() > 0);
    }
    else
    {
        const auto p = engine.chainParameter (place, kn.parameter);
        position = (int) std::lround (p.value * kn.dial->maximum());
        text = QString::fromStdString (p.valueText);
    }

    if (! kn.dial->isSliderDown())
        kn.dial->setValue (position);
    kn.value->setText (elided (text, kn.value->font(), kn.value->width()));
    kn.value->setToolTip (text);
}

void DeviceCard::sync()
{
    if (cardKind == Kind::add)
        return;

    syncing = true;
    for (auto& kn : knobs)
        setKnobFromEngine (kn);

    if (cardKind == Kind::plugin)
    {
        const auto info = engine.chainDevice (place);
        const auto soloed = place.topLevel() && engine.masterSoloSlot() == place.slot;
        power->setChecked (! info.bypassed);
        power->setText (info.bypassed ? tr ("Off") : tr ("On"));
        if (solo != nullptr)
            solo->setChecked (soloed);
        windowButton->setVisible ((info.hasEditor || info.failed) && ! isPrism);   // built-in effects have only their knobs
        windowButton->setText (info.failed ? tr ("Reload") : tr ("Window"));
        windowButton->setToolTip (info.failed ? tr ("Load the plugin again with its last settings (E)") : tr ("The plugin's own window (E)"));

        auto text = QString::fromStdString (info.format);
        if (isPrism)
        {
            const auto prism = engine.prismInfo (place);
            text = prismSummary (prism);
            for (int m = 0; m < (int) knobs.size() && m < PrismInfo::numMacros; ++m)
            {
                const auto& macro = prism.macros[(size_t) m];
                const auto macroName = QString::fromStdString (macro.name);
                if (knobs[(size_t) m].name->toolTip() != macroName)
                {
                    knobs[(size_t) m].name->setText (elided (macroName, knobs[(size_t) m].name->font(), knobs[(size_t) m].name->width() - 2));
                    knobs[(size_t) m].name->setToolTip (macroName);
                    knobs[(size_t) m].dial->setAccessibleName (macroName);
                }
            }
            if (title->toolTip() != QString::fromStdString (prism.name))
            {
                title->setText (elided (QString::fromStdString (prism.name), title->font(), 300));
                title->setToolTip (QString::fromStdString (prism.name));
            }
            std::vector<PrismStrip::Ray> shown;
            for (int r = 0; r < (int) prism.rays.size(); ++r)
            {
                const auto& ray = prism.rays[(size_t) r];
                shown.push_back ({ QString::fromStdString (ray.name), ray.heard, ray.muted, ray.soloed,
                                   engine.prismTakeRayPeak (place, r), (int) ray.devices.size() });
            }
            rays->set (shown);
        }
        if (info.latencySamples > 0)
            text += tr ("  %1 smp latency").arg (info.latencySamples);
        if (info.channels > 0 && info.channels < engine.masterChannels())
            text += info.channels <= 2 ? tr ("  L/R only") : tr ("  first %1 ch").arg (info.channels);
        if (soloed)
            text += tr ("  SOLO");
        if (info.failed)
        {
            text = tr ("Stopped working: %1").arg (QString::fromStdString (info.failure));
            detail->setToolTip (tr ("The plugin stopped answering (for a Windows plugin, its Wine side may have closed), "
                                    "so it is switched off and the sound passes through. Reload loads it again with its last settings."));
        }
        else
            detail->setToolTip ({});
        detail->setText (elided (text, detail->font(), width() - 20));
        detail->setStyleSheet (info.failed ? QStringLiteral ("color: %1").arg (Theme::current().color (QStringLiteral ("meterHigh")).name())
                                           : QString());
        title->setForegroundRole (info.bypassed || info.failed || engine.isMasterBypassed() ? QPalette::PlaceholderText : QPalette::WindowText);
        level->push (engine.takeChainPeak (place));
    }
    else
    {
        power->setChecked (! engine.isMasterBypassed());
        power->setText (engine.isMasterBypassed() ? tr ("Off") : tr ("On"));
        detail->setText (engine.isMasterBypassed() ? tr ("Bypassed") : (engine.masterSoloSlot() >= 0 ? tr ("Solo active") : QString()));
    }
    syncing = false;
}

void DeviceCard::nudge (int index, double steps)
{
    if (index < 0 || index >= (int) knobs.size())
        return;

    auto& kn = knobs[(size_t) index];
    const auto perStep = kn.parameter < 0 || kn.steps > 1 ? 1.0 : dialResolution / 100.0;   // master knobs: half a dB, or one choice
    const auto target = std::clamp ((int) std::lround (kn.dial->value() + steps * perStep), kn.dial->minimum(), kn.dial->maximum());
    kn.dial->setValue (target);  // valueChanged sends it to the engine
}

void DeviceCard::editValue (int index, const QString& startText)
{
    if (index < 0 || index >= (int) knobs.size() || valueEditor != nullptr)
        return;

    const auto& kn = knobs[(size_t) index];
    QString current;
    if (kn.parameter == masterGain)
        current = QString::number (engine.masterGainDb(), 'f', 1);
    else if (kn.parameter < 0)
        current = kn.value->text();
    else
        current = QString::fromStdString (engine.chainParameter (place, kn.parameter).valueText);

    valueEditor = new QLineEdit (this);
    valueEditor->setAccessibleName (tr ("Type a value for %1").arg (kn.dial->accessibleName()));
    valueEditor->setToolTip (tr ("Type a value, then Enter. Esc cancels."));
    valueEditor->setText (startText.isEmpty() ? current : startText);
    valueEditor->setAlignment (Qt::AlignHCenter);

    // Over the knob's value, a little wider so units fit.
    auto r = kn.value->geometry().adjusted (-14, -3, 14, 3);
    r.moveLeft (std::clamp (r.left(), 2, width() - r.width() - 2));
    valueEditor->setGeometry (r);
    valueEditor->installEventFilter (this);
    valueEditor->show();
    valueEditor->setFocus (Qt::OtherFocusReason);
    if (startText.isEmpty())
        valueEditor->selectAll();

    connect (valueEditor, &QLineEdit::editingFinished, this, [this, index]
    {
        auto* editor = valueEditor;
        if (editor == nullptr)
            return;

        valueEditor = nullptr;
        if (! editor->property ("cancelled").toBool())
            applyTypedValue (index, editor->text());
        editor->hide();
        editor->deleteLater();
        Q_EMIT valueEditFinished();
    });
}

void DeviceCard::applyTypedValue (int index, const QString& text)
{
    const auto& kn = knobs[(size_t) index];

    if (kn.parameter == ditherDepth || kn.parameter == ditherNoise)
    {
        const auto t = text.trimmed().toLower();
        if (kn.parameter == ditherDepth)
        {
            const auto bits = t.startsWith (QStringLiteral ("off")) || t == QStringLiteral ("0") ? 0 : t.left (2).toInt();
            if (bits != 0 && bits != 16 && bits != 20 && bits != 24)
            {
                Q_EMIT message (tr ("Type 16, 20, 24 or off"));
                return;
            }
            engine.setMasterDither (bits, engine.masterDitherType());
        }
        else
        {
            const auto type = t.startsWith (QStringLiteral ("t")) ? dsp::Dither::tpdf
                            : t.startsWith (QStringLiteral ("s")) ? dsp::Dither::shaped
                            : t.startsWith (QStringLiteral ("r")) || t.startsWith (QStringLiteral ("n")) || t.startsWith (QStringLiteral ("p")) ? dsp::Dither::none
                                                                                                                                            : (dsp::Dither) -1;
            if ((int) type < 0)
            {
                Q_EMIT message (tr ("Type TPDF, shaped or round"));
                return;
            }
            engine.setMasterDither (engine.masterDitherBits(), type);
        }
    }
    else if (kn.parameter < 0)
    {
        bool ok = false;
        auto cleaned = text;
        cleaned.remove (QStringLiteral ("dB"), Qt::CaseInsensitive);
        const auto db = cleaned.trimmed().toDouble (&ok);
        if (! ok)
        {
            Q_EMIT message (tr ("Type a number of decibels, for example -3 or 1.5"));
            return;
        }
        engine.setMasterGainDb (std::clamp (db, -60.0, 24.0));
    }
    else if (! engine.setChainParameterText (place, kn.parameter, text.toStdString()))
    {
        Q_EMIT message (tr ("%1 does not understand \"%2\"").arg (kn.dial->accessibleName(), text));
        return;
    }

    sync();
    Q_EMIT changed();
}

void DeviceCard::setSelected (bool isSelected, bool rowFocused, int knob)
{
    if (selected == isSelected && focused == rowFocused && selectedKnob == knob)
        return;

    selected = isSelected;
    focused = rowFocused;
    selectedKnob = isSelected ? knob : -1;

    for (int i = 0; i < (int) knobs.size(); ++i)
    {
        auto f = knobs[(size_t) i].name->font();
        f.setBold (i == selectedKnob);
        f.setUnderline (i == selectedKnob && focused);
        knobs[(size_t) i].name->setFont (f);
        knobs[(size_t) i].name->setForegroundRole (i == selectedKnob && focused ? QPalette::Link : QPalette::WindowText);
    }

    update();
}

void DeviceCard::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto r = QRectF (rect()).adjusted (1.5, 1.5, -1.5, -1.5);

    g.setPen (Qt::NoPen);
    g.setBrush (palette().color (cardKind == Kind::add ? QPalette::Window : QPalette::Base));
    g.drawRoundedRect (r, 6, 6);

    // A card in a range picked with Shift (to group into a Prism) is tinted.
    if (inRange && ! selected)
    {
        auto tint = palette().color (QPalette::Highlight);
        tint.setAlphaF (0.12);
        g.setBrush (tint);
        g.drawRoundedRect (r, 6, 6);
    }

    auto pen = QPen (palette().color (inRange ? QPalette::Highlight : QPalette::Mid), 1.0);
    if (selected)
        pen = QPen (palette().color (focused ? QPalette::Link : QPalette::Highlight), focused ? 2.0 : 1.5);
    g.setPen (pen);
    g.setBrush (Qt::NoBrush);
    g.drawRoundedRect (r, 6, 6);

    // A Prism that is popped out for editing stays lit where it docks.
    if (editing)
    {
        auto glow = palette().color (QPalette::Highlight);
        for (int i = 3; i >= 1; --i)
        {
            glow.setAlphaF (0.14 * (4 - i));
            g.setPen (QPen (glow, i * 2.0));
            g.drawRoundedRect (r.adjusted (i, i, -i, -i), 6, 6);
        }
        g.setPen (QPen (palette().color (QPalette::Highlight), 1.5, Qt::DashLine));
        g.drawRoundedRect (r, 6, 6);
    }
}

void DeviceCard::setInRange (bool b)
{
    if (inRange != b)
    {
        inRange = b;
        update();
    }
}

void DeviceCard::setEditing (bool b)
{
    if (editing != b)
    {
        editing = b;
        update();
    }
}

int DeviceCard::parameterOf (int index) const
{
    return index >= 0 && index < (int) knobs.size() ? knobs[(size_t) index].parameter : -1;
}

QString DeviceCard::prismSummary (const PrismInfo& prism)
{
    static const char* splits[] { QT_TR_NOOP ("Parallel"), QT_TR_NOOP ("Bands"), QT_TR_NOOP ("Mid/Side"), QT_TR_NOOP ("Left/Right") };
    int heard = 0;
    for (const auto& r : prism.rays)
        heard += r.heard ? 1 : 0;
    auto text = tr ("Prism  %1").arg (tr (splits[std::clamp (prism.split, 0, 3)]));
    if (prism.split == PrismInfo::bands)
    {
        QStringList hz;
        for (int i = 0; i + 1 < heard && i < 3; ++i)
        {
            const auto f = prism.crossoverHz[(size_t) i];
            hz << (f >= 1000.0 ? QString::number (f / 1000.0, 'f', 1) + QStringLiteral ("k") : QString::number (std::lround (f)));
        }
        text += QStringLiteral ("  ") + hz.join (QStringLiteral (" / "));
    }
    else
        text += prism.rays.size() == 1 ? tr ("  1 ray") : tr ("  %1 rays").arg (prism.rays.size());
    if (prism.latencySamples > 0)
        text += tr ("  %1 smp").arg (prism.latencySamples);
    return text;
}

//==============================================================================
PrismStrip::PrismStrip (QWidget* parent) : QWidget (parent)
{
    setFixedHeight (12);
    setToolTip (tr ("The Prism's rays and their levels"));
}

void PrismStrip::set (const std::vector<Ray>& next)
{
    // Levels fall back smoothly, like the level bars.
    std::vector<float> falling;
    for (size_t i = 0; i < next.size(); ++i)
    {
        const auto db = next[i].peak > 0.0f ? 20.0f * std::log10 (next[i].peak) : -100.0f;
        const auto before = i < shownDb.size() ? shownDb[i] : -100.0f;
        falling.push_back (std::max (db, before - 3.0f));
    }
    shownDb = falling;
    rays = next;
    update();
}

void PrismStrip::paintEvent (QPaintEvent*)
{
    if (rays.empty())
        return;
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto& theme = Theme::current();
    const auto gap = 3.0;
    const auto w = (width() - gap * ((double) rays.size() - 1)) / (double) rays.size();
    // One bar per ray, its colour fading through a spectrum like light through a prism.
    for (size_t i = 0; i < rays.size(); ++i)
    {
        const QRectF bar (i * (w + gap), 2.0, w, height() - 4.0);
        auto hue = QColor::fromHsvF (std::fmod (0.62 - 0.11 * (double) i + 1.0, 1.0), 0.55, 0.9);
        auto back = Theme::withOpacity (theme.color (QStringLiteral ("meterBackground")));
        g.setPen (Qt::NoPen);
        g.setBrush (back);
        g.drawRoundedRect (bar, 2, 2);
        if (! rays[i].heard || rays[i].muted)
            hue.setAlphaF (0.25);
        const auto fraction = std::clamp ((shownDb[i] + 60.0f) / 60.0f, 0.0f, 1.0f);
        g.setBrush (hue);
        g.drawRoundedRect (QRectF (bar.left(), bar.top(), std::max (2.0, bar.width() * fraction), bar.height()), 2, 2);
        if (rays[i].soloed)
        {
            g.setPen (QPen (palette().color (QPalette::Highlight), 1.2));
            g.setBrush (Qt::NoBrush);
            g.drawRoundedRect (bar, 2, 2);
        }
    }
}

bool DeviceCard::eventFilter (QObject* watched, QEvent* e)
{
    if (watched == valueEditor && e->type() == QEvent::KeyPress)
    {
        // Finish here, so the key is not passed on to the row (where Enter opens the parameter editor).
        const auto key = static_cast<QKeyEvent*> (e)->key();
        if (key == Qt::Key_Escape)
            valueEditor->setProperty ("cancelled", true);
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter)
        {
            valueEditor->clearFocus();  // ends the edit; applies it unless cancelled
            return true;
        }
    }

    if (e->type() == QEvent::MouseButtonDblClick)
    {
        for (int i = 0; i < (int) knobs.size(); ++i)
            if (watched == knobs[(size_t) i].dial || watched == knobs[(size_t) i].name || watched == knobs[(size_t) i].value)
            {
                Q_EMIT activated (i);
                editValue (i);
                return true;
            }
    }

    if (e->type() == QEvent::MouseButtonPress)
    {
        int index = -1;
        for (int i = 0; i < (int) knobs.size(); ++i)
            if (watched == knobs[(size_t) i].dial || watched == knobs[(size_t) i].name || watched == knobs[(size_t) i].value)
                index = i;
        Q_EMIT activated (index);

        // A knob's labels would pass the press on to the card, which selects the card instead.
        if (index >= 0 && qobject_cast<QLabel*> (watched) != nullptr)
            return true;
    }

    // Other labels pass presses on to the card, so dragging by the title moves the plugin.
    return false;
}

void DeviceCard::mousePressEvent (QMouseEvent* e)
{
    pressPos = e->position().toPoint();
    Q_EMIT activated (-1);
}

void DeviceCard::mouseMoveEvent (QMouseEvent* e)
{
    if (cardKind != Kind::plugin || ! (e->buttons() & Qt::LeftButton)
        || (e->position().toPoint() - pressPos).manhattanLength() < QApplication::startDragDistance())
        return;

    // In the chain, the slot; in a Prism, the ray and the device.
    auto* mime = new QMimeData;
    if (place.topLevel())
        mime->setData (slotMime, QByteArray::number (place.slot));
    else
        mime->setData (prismDeviceMime(), QByteArray::number (place.steps.back().first) + ',' + QByteArray::number (place.steps.back().second));
    auto* drag = new QDrag (this);
    drag->setMimeData (mime);
    drag->setPixmap (grab().scaledToWidth (width() / 2, Qt::SmoothTransformation));
    drag->exec (Qt::MoveAction);
}

//==============================================================================
ChainRow::ChainRow (AudioEngine& e, QWidget* homeWidget, QWidget* parent)
    : QWidget (parent), engine (e), home (homeWidget), scroll (new QScrollArea (this)), strip (new QWidget)
{
    setAccessibleName (tr ("Device chain"));
    setAccessibleDescription (tr ("Left and Right pick a plugin, Up and Down pick a knob, plus and minus turn it, F2 or a number "
                                  "types an exact value, Enter edits all "
                                  "parameters, Insert adds a plugin, Delete removes it, B bypasses, S solos, E opens its window, "
                                  "Control with Left or Right moves it."));
    setFocusPolicy (Qt::StrongFocus);
    setAcceptDrops (true);
    setMinimumHeight (175);

    stripLayout = new QHBoxLayout (strip);
    stripLayout->setContentsMargins (6, 4, 6, 4);
    stripLayout->setSpacing (4);
    strip->setAutoFillBackground (false);

    scroll->setWidget (strip);
    scroll->setWidgetResizable (true);
    scroll->setFrameShape (QFrame::NoFrame);
    scroll->setFocusPolicy (Qt::NoFocus);
    scroll->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground (false);
    scroll->horizontalScrollBar()->setFocusPolicy (Qt::NoFocus);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->addWidget (scroll);

    connect (&timer, &QTimer::timeout, this, [this]
    {
        if (signature() != lastSignature)
        {
            // Say once when a plugin stops working; its card keeps saying so.
            for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
            {
                const auto info = engine.masterSlot (i);
                const auto bit = 1 << i;
                if (info.failed && (failedSlots & bit) == 0)
                    Q_EMIT message (tr ("%1 stopped working (%2). It is switched off and the sound passes through; "
                                        "press Reload on its card (or E) to load it again.")
                                        .arg (QString::fromStdString (info.name), QString::fromStdString (info.failure)));
                failedSlots = info.failed ? failedSlots | bit : failedSlots & ~bit;
            }
            rebuild();
        }
        else
            for (auto* c : cards)
                c->sync();
    });
    timer.start (100);
    rebuild();
}

QString ChainRow::signature() const
{
    QString s = engine.isMasterBypassed() ? QStringLiteral ("B") : QStringLiteral ("-");
    for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
    {
        const auto info = engine.masterSlot (i);
        s += QLatin1Char ('|') + QString::fromStdString (info.pluginId) + (info.failed ? QStringLiteral ("!") : QString());
    }
    return s;
}

void ChainRow::rebuild()
{
    const auto* old = currentCard();
    const auto oldSlot = old != nullptr ? old->slot() : -1;
    const auto oldKind = old != nullptr ? old->kind() : DeviceCard::Kind::plugin;

    for (auto* c : cards)
        c->deleteLater();
    cards.clear();
    while (auto* item = stripLayout->takeAt (0))
    {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto addArrow = [this]
    {
        auto* arrow = new QLabel (QStringLiteral ("›"), strip);  // single right-pointing angle quote
        arrow->setForegroundRole (QPalette::PlaceholderText);
        stripLayout->addWidget (arrow);
    };

    auto addCard = [this] (DeviceCard::Kind kind, int slot)
    {
        auto* card = new DeviceCard (engine, kind, ChainPath { slot, {} }, strip);
        const auto index = (int) cards.size();
        cards.push_back (card);
        stripLayout->addWidget (card);

        connect (card, &DeviceCard::activated, this, [this, index] (int k)
        {
            select (index, k);
            setFocus (Qt::MouseFocusReason);
        });
        connect (card, &DeviceCard::changed, this, [this]
        {
            for (auto* c : cards)
                c->sync();
            Q_EMIT chainChanged();
        });
        connect (card, &DeviceCard::editRequested, this, [this, index, slot]
        {
            if (cards[(size_t) index]->prism())
                openPrism (index);
            else
                editParameters (slot);
        });
        connect (card, &DeviceCard::message, this, &ChainRow::message);
        connect (card, &DeviceCard::valueEditFinished, this, [this] { setFocus (Qt::OtherFocusReason); });
        connect (card, &DeviceCard::windowRequested, this, [this, index, slot]
        {
            if (cards[(size_t) index]->prism())
                openPrism (index);
            else
                showWindow (slot);
        });
        if (card->prism())
            card->setEditing (editors.count (engine.prismInfo (card->path()).uid) > 0);
        connect (card, &DeviceCard::addRequested, this, [this] { insertAfter (lastPluginSlot()); });
    };

    for (int i = 0; i < AudioEngine::numMasterSlots; ++i)
    {
        if (! engine.masterSlot (i).empty)
        {
            addCard (DeviceCard::Kind::plugin, i);
            addArrow();
        }
    }

    addCard (DeviceCard::Kind::add, -1);
    addArrow();
    addCard (DeviceCard::Kind::master, -1);
    stripLayout->addStretch (1);

    lastSignature = signature();

    // Keep the same card selected when it still exists; otherwise start at the first card.
    int index = oldKind == DeviceCard::Kind::plugin ? std::min (current, (int) cards.size() - 1) : 0;
    for (int i = 0; i < (int) cards.size(); ++i)
        if (old != nullptr && oldKind != DeviceCard::Kind::add && cards[(size_t) i]->kind() == oldKind
            && (oldKind != DeviceCard::Kind::plugin || cards[(size_t) i]->slot() == oldSlot))
            index = i;

    current = -1;
    anchor = -1;
    select (index, knob);
}

DeviceCard* ChainRow::currentCard() const
{
    return current >= 0 && current < (int) cards.size() ? cards[(size_t) current] : nullptr;
}

int ChainRow::lastPluginSlot() const
{
    int last = -1;
    for (auto* c : cards)
        if (c->kind() == DeviceCard::Kind::plugin)
            last = c->slot();
    return last;
}

void ChainRow::select (int card, int k)
{
    if (cards.empty())
        return;

    anchor = -1;
    current = std::clamp (card, 0, (int) cards.size() - 1);
    auto* c = cards[(size_t) current];
    knob = c->numKnobs() == 0 ? -1 : std::clamp (k, -1, c->numKnobs() - 1);
    showSelection();
    scroll->ensureWidgetVisible (c, 8, 0);
}

void ChainRow::showSelection()
{
    const auto [first, last] = range();
    for (int i = 0; i < (int) cards.size(); ++i)
    {
        cards[(size_t) i]->setSelected (i == current, hasFocus(), knob);
        cards[(size_t) i]->setInRange (anchor >= 0 && first != last && i >= first && i <= last);
    }
}

std::pair<int, int> ChainRow::range() const
{
    if (anchor < 0)
        return { current, current };
    return { std::min (anchor, current), std::max (anchor, current) };
}

void ChainRow::focusInEvent (QFocusEvent* e)
{
    QWidget::focusInEvent (e);
    showSelection();
}

void ChainRow::focusOutEvent (QFocusEvent* e)
{
    QWidget::focusOutEvent (e);
    showSelection();
}

bool ChainRow::handles (const QKeyEvent* k)
{
    const auto mods = k->modifiers() & ~Qt::KeypadModifier;
    const auto key = k->key();

    if (mods == Qt::ControlModifier)
        return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_G;

    if (mods == (Qt::ControlModifier | Qt::ShiftModifier))
        return key == Qt::Key_G;

    if (mods == Qt::ShiftModifier)
        return key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus || key == Qt::Key_Underscore
            || key == Qt::Key_Left || key == Qt::Key_Right;

    if (mods != Qt::NoModifier)
        return false;

    switch (key)
    {
        case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
        case Qt::Key_Plus: case Qt::Key_Equal: case Qt::Key_Minus: case Qt::Key_PageUp: case Qt::Key_PageDown:
        case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Insert: case Qt::Key_Delete: case Qt::Key_Backspace:
        case Qt::Key_B: case Qt::Key_S: case Qt::Key_E: case Qt::Key_Escape:
        case Qt::Key_F2: case Qt::Key_Period:
        case Qt::Key_0: case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4:
        case Qt::Key_5: case Qt::Key_6: case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
            return true;
        default:
            return false;  // Space and everything else stay with the editor's shortcuts
    }
}

bool ChainRow::event (QEvent* e)
{
    // Claim the keys this row uses before the window's shortcuts see them.
    if (e->type() == QEvent::ShortcutOverride && handles (static_cast<QKeyEvent*> (e)))
    {
        e->accept();
        return true;
    }

    return QWidget::event (e);
}

void ChainRow::keyPressEvent (QKeyEvent* k)
{
    if (! handles (k))
    {
        QWidget::keyPressEvent (k);
        return;
    }

    auto* card = currentCard();
    const auto ctrl = (k->modifiers() & Qt::ControlModifier) != 0;
    const auto shift = (k->modifiers() & Qt::ShiftModifier) != 0;
    const auto isPlugin = card != nullptr && card->kind() == DeviceCard::Kind::plugin;

    switch (k->key())
    {
        case Qt::Key_Escape:
            home->window()->activateWindow();
            home->setFocus (Qt::ShortcutFocusReason);
            break;

        case Qt::Key_G:
            if (shift)
                ungroupCurrent();
            else
                groupSelection();
            break;

        case Qt::Key_Left:
        case Qt::Key_Right:
        {
            const auto delta = k->key() == Qt::Key_Left ? -1 : 1;
            if (shift)
            {
                // Picks a range of plugins (to group): the add and master cards are not part of it.
                const auto next = current + delta;
                if (! isPlugin || next < 0 || next >= (int) cards.size() || cards[(size_t) next]->kind() != DeviceCard::Kind::plugin)
                    break;
                if (anchor < 0)
                    anchor = current;
                const auto keep = anchor;
                select (next, knob);
                anchor = keep;
                showSelection();
                break;
            }
            if (! ctrl)
            {
                select (current + delta, knob);
                break;
            }

            // Move the plugin past its neighbour.
            const auto neighbour = current + delta;
            if (isPlugin && neighbour >= 0 && neighbour < (int) cards.size()
                && cards[(size_t) neighbour]->kind() == DeviceCard::Kind::plugin)
                movePlugin (card->slot(), cards[(size_t) neighbour]->slot());
            break;
        }

        case Qt::Key_Up:   select (current, knob - 1); break;
        case Qt::Key_Down: select (current, knob + 1); break;

        case Qt::Key_Plus: case Qt::Key_Equal:
        case Qt::Key_Minus: case Qt::Key_Underscore:
        case Qt::Key_PageUp: case Qt::Key_PageDown:
        {
            if (card == nullptr || card->numKnobs() == 0)
                break;
            if (knob < 0)
                select (current, 0);

            const auto up = k->key() == Qt::Key_Plus || k->key() == Qt::Key_Equal || k->key() == Qt::Key_PageUp;
            const auto big = shift || k->key() == Qt::Key_PageUp || k->key() == Qt::Key_PageDown;
            card->nudge (knob, (up ? 1.0 : -1.0) * (big ? 10.0 : 1.0));
            break;
        }

        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (isPlugin && card->prism())
                openPrism (current);
            else if (isPlugin)
                editParameters (card->slot());
            else if (card != nullptr && card->kind() == DeviceCard::Kind::add)
                insertAfter (lastPluginSlot());
            break;

        case Qt::Key_Insert:
            insertAfter (isPlugin ? card->slot() : lastPluginSlot());
            break;

        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            removeCurrent();
            break;

        case Qt::Key_F2:
        case Qt::Key_Period:
        case Qt::Key_0: case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4:
        case Qt::Key_5: case Qt::Key_6: case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
        {
            // F2, or just start typing a number, to set the selected knob exactly.
            if (card == nullptr || card->numKnobs() == 0)
                break;
            if (knob < 0)
                select (current, 0);
            card->editValue (knob, k->key() == Qt::Key_F2 ? QString() : k->text());
            break;
        }

        case Qt::Key_B: toggleBypass(); break;
        case Qt::Key_S: toggleSolo(); break;
        case Qt::Key_E:
            if (isPlugin && card->prism())
                openPrism (current);
            else if (isPlugin)
                showWindow (card->slot());
            break;

        default:
            break;
    }
}

void ChainRow::insertAfter (int slot)
{
    int empty = -1;
    for (int i = 0; i < AudioEngine::numMasterSlots && empty < 0; ++i)
        if (engine.masterSlot (i).empty)
            empty = i;

    if (empty < 0)
    {
        Q_EMIT message (tr ("All %1 slots are in use; remove a plugin first").arg (AudioEngine::numMasterSlots));
        return;
    }

    if (! choosePlugin || ! choosePlugin (empty))
    {
        setFocus (Qt::OtherFocusReason);
        return;
    }

    // Moving shifts the plugins in between, so the new one lands right after `slot`.
    int target = empty;
    if (slot < 0)
        target = 0;
    else if (empty > slot)
        target = slot + 1;
    else if (empty < slot)
        target = slot;

    if (target != empty)
        engine.moveMasterSlot (empty, target);

    rebuild();
    for (int i = 0; i < (int) cards.size(); ++i)
        if (cards[(size_t) i]->kind() == DeviceCard::Kind::plugin && cards[(size_t) i]->slot() == target)
            select (i, -1);

    Q_EMIT chainChanged();
    setFocus (Qt::OtherFocusReason);
    // A new plugin opens in its own floating window (Esc closes it and the keys come back to the chain).
    if (engine.canShowPluginWindow (target))
        engine.showPluginWindow (target);
    else
        window()->activateWindow();
}

void ChainRow::movePlugin (int from, int to)
{
    if (from == to || from < 0 || to < 0)
        return;

    engine.moveMasterSlot (from, to);
    rebuild();
    for (int i = 0; i < (int) cards.size(); ++i)
        if (cards[(size_t) i]->kind() == DeviceCard::Kind::plugin && cards[(size_t) i]->slot() == to)
            select (i, knob);

    Q_EMIT chainChanged();
}

void ChainRow::removeCurrent()
{
    auto* card = currentCard();
    if (card == nullptr || card->kind() != DeviceCard::Kind::plugin)
        return;

    const auto name = QString::fromStdString (engine.masterSlot (card->slot()).name);
    engine.clearMasterSlot (card->slot());
    rebuild();
    Q_EMIT chainChanged();
    Q_EMIT message (tr ("Removed %1").arg (name));
}

void ChainRow::toggleBypass()
{
    auto* card = currentCard();
    if (card == nullptr)
        return;

    if (card->kind() == DeviceCard::Kind::plugin)
    {
        const auto info = engine.masterSlot (card->slot());
        engine.setMasterSlotBypassed (card->slot(), ! info.bypassed);
        Q_EMIT message (info.bypassed ? tr ("%1 on").arg (QString::fromStdString (info.name))
                                      : tr ("%1 bypassed").arg (QString::fromStdString (info.name)));
    }
    else if (card->kind() == DeviceCard::Kind::master)
        engine.setMasterBypassed (! engine.isMasterBypassed());
    else
        return;

    for (auto* c : cards)
        c->sync();
    Q_EMIT chainChanged();
}

void ChainRow::toggleSolo()
{
    auto* card = currentCard();
    if (card == nullptr || card->kind() != DeviceCard::Kind::plugin)
    {
        if (engine.masterSoloSlot() >= 0)
            engine.setMasterSoloSlot (-1);
    }
    else
    {
        const auto on = engine.masterSoloSlot() != card->slot();
        engine.setMasterSoloSlot (on ? card->slot() : -1);
        Q_EMIT message (on ? tr ("Solo: hearing only %1 (renders still use the whole chain)")
                                 .arg (QString::fromStdString (engine.masterSlot (card->slot()).name))
                           : tr ("Solo off"));
    }

    for (auto* c : cards)
        c->sync();
    Q_EMIT chainChanged();
}

void ChainRow::editParameters (int slot)
{
    if (engine.masterSlot (slot).empty)
        return;

    auto* dialog = new ParameterDialog (engine, slot, window());
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->show();
}

void ChainRow::showWindow (int slot)
{
    const auto info = engine.masterSlot (slot);
    if (info.failed)
    {
        const auto error = engine.reloadMasterSlot (slot);
        Q_EMIT message (error.empty() ? tr ("%1 loaded again with its last settings").arg (QString::fromStdString (info.name))
                                      : QString::fromStdString (error));
        Q_EMIT chainChanged();
    }
    else if (engine.canShowPluginWindow (slot))
        engine.showPluginWindow (slot);
    else if (! info.empty)
        Q_EMIT message (engine.pluginWindowsLost()
                            ? tr ("Plugin windows were force-closed by the window manager; restart PHI to open them again (Enter edits parameters meanwhile)")
                        : info.hasEditor ? tr ("Plugin windows need XWayland or X11; use Enter for the parameter editor")
                                         : tr ("This plugin has no window of its own; use Enter for the parameter editor"));
}

int ChainRow::cardAt (const QPoint& pos) const
{
    for (int i = 0; i < (int) cards.size(); ++i)
    {
        const auto r = QRect (cards[(size_t) i]->mapTo (this, QPoint (0, 0)), cards[(size_t) i]->size());
        if (pos.x() < r.right())
            return i;
    }
    return (int) cards.size() - 1;
}

void ChainRow::dragEnterEvent (QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat (slotMime))
        e->acceptProposedAction();
}

void ChainRow::dragMoveEvent (QDragMoveEvent* e)
{
    if (e->mimeData()->hasFormat (slotMime))
        e->acceptProposedAction();
}

void ChainRow::dropEvent (QDropEvent* e)
{
    if (! e->mimeData()->hasFormat (slotMime))
        return;

    const auto from = e->mimeData()->data (slotMime).toInt();
    const auto index = cardAt (e->position().toPoint());
    auto to = cards[(size_t) index]->kind() == DeviceCard::Kind::plugin ? cards[(size_t) index]->slot() : lastPluginSlot();
    e->acceptProposedAction();
    movePlugin (from, to);
    setFocus (Qt::MouseFocusReason);
}

//==============================================================================
void ChainRow::groupSelection()
{
    const auto [first, last] = range();
    if (first < 0 || last >= (int) cards.size() || cards[(size_t) first]->kind() != DeviceCard::Kind::plugin)
    {
        Q_EMIT message (tr ("Pick a plugin (Shift+Left/Right for several), then Ctrl+G groups them into a Prism"));
        return;
    }
    int lastSlot = cards[(size_t) first]->slot();
    for (int i = first; i <= last; ++i)
        if (cards[(size_t) i]->kind() == DeviceCard::Kind::plugin)
            lastSlot = cards[(size_t) i]->slot();

    const auto firstSlot = cards[(size_t) first]->slot();
    const auto count = last - first + 1;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto error = engine.groupMasterSlots (firstSlot, lastSlot);
    QApplication::restoreOverrideCursor();
    if (! error.empty())
    {
        Q_EMIT message (QString::fromStdString (error));
        return;
    }

    rebuild();
    for (int i = 0; i < (int) cards.size(); ++i)
        if (cards[(size_t) i]->kind() == DeviceCard::Kind::plugin && cards[(size_t) i]->slot() == firstSlot)
            select (i, -1);
    Q_EMIT chainChanged();
    Q_EMIT message ((count == 1 ? tr ("Put the plugin in a Prism") : tr ("Grouped %1 plugins into a Prism").arg (count))
                    + tr (". Enter opens it; Ctrl+Shift+G ungroups"));
}

void ChainRow::ungroupCurrent()
{
    auto* card = currentCard();
    if (card == nullptr || ! card->prism())
    {
        Q_EMIT message (tr ("Ctrl+Shift+G ungroups a Prism; pick one first"));
        return;
    }
    const auto slot = card->slot();
    if (auto it = editors.find (engine.prismInfo (card->path()).uid); it != editors.end() && it->second != nullptr)
        it->second->dockNow();

    const auto error = engine.ungroupMasterSlot (slot);
    rebuild();
    for (int i = 0; i < (int) cards.size(); ++i)
        if (cards[(size_t) i]->kind() == DeviceCard::Kind::plugin && cards[(size_t) i]->slot() == slot)
            select (i, -1);
    Q_EMIT chainChanged();
    Q_EMIT message (error.empty() ? tr ("Prism ungrouped") : QString::fromStdString (error));
}

void ChainRow::openPrism (int index)
{
    if (index < 0 || index >= (int) cards.size() || ! cards[(size_t) index]->prism())
        return;

    auto* card = cards[(size_t) index];
    const auto uid = engine.prismInfo (card->path()).uid;
    // A second press docks it again.
    if (auto it = editors.find (uid); it != editors.end() && it->second != nullptr)
    {
        it->second->dock();
        return;
    }

    auto* editor = new PrismEditor (engine, card->slot(), uid, window());
    editor->pickPlugin = pickPlugin;
    editors[uid] = editor;
    card->setEditing (true);
    connect (editor, &PrismEditor::message, this, &ChainRow::message);
    connect (editor, &PrismEditor::chainChanged, this, &ChainRow::chainChanged);
    connect (editor, &PrismEditor::docked, this, [this, uid] (bool returnFocus)
    {
        editors.erase (uid);
        for (auto* c : cards)
            if (c->prism() && engine.prismInfo (c->path()).uid == uid)
            {
                c->setEditing (false);
                if (returnFocus)
                    for (int i = 0; i < (int) cards.size(); ++i)
                        if (cards[(size_t) i] == c)
                            select (i, knob);
            }
        if (returnFocus)
            setFocus (Qt::OtherFocusReason);
    });
    // It docks into whichever card holds its Prism now (the Prism may have moved).
    editor->findCard = [this] (std::uint64_t id) -> QWidget*
    {
        for (auto* c : cards)
            if (c->prism() && engine.prismInfo (c->path()).uid == id)
                return c;
        return nullptr;
    };
    editor->popOut (card);
}
