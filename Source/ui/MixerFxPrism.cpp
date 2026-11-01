// SPDX-License-Identifier: AGPL-3.0-only
// Effect Prisms in the mixer's returns (docs/prism.md, owner 2026-11-01: "allow for either one of
// the FX sends to be a Prism, but with fx only"): what the sends bring in is divided among rays of
// effects (in parallel, in bands, as mid and side, or left and right), and joined again.
#include "MixerWindow.h"
#include "../engine/AudioEngine.h"
#include "../mixer/EffectPrism.h"

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    using FxRay = mixer::EffectPrism::Ray;

    QString splitLabel (prism::Split s)
    {
        switch (s)
        {
            case prism::Split::parallel:  return MixerWindow::tr ("Parallel");
            case prism::Split::bands:     return MixerWindow::tr ("Bands");
            case prism::Split::midSide:   return MixerWindow::tr ("Mid/Side");
            case prism::Split::leftRight: return MixerWindow::tr ("Left/Right");
        }
        return {};
    }

    // What a ray is called by its place in the split, until it is named.
    QString rayRole (prism::Split s, int ray, int heard)
    {
        if (ray >= heard)
            return MixerWindow::tr ("not heard in this split");
        switch (s)
        {
            case prism::Split::parallel:  return {};
            case prism::Split::bands:
                if (heard <= 1)
                    return MixerWindow::tr ("all of it");
                if (ray == 0)
                    return MixerWindow::tr ("lowest band");
                return ray == heard - 1 ? MixerWindow::tr ("highest band") : MixerWindow::tr ("band %1").arg (ray + 1);
            case prism::Split::midSide:   return ray == 0 ? MixerWindow::tr ("mid") : MixerWindow::tr ("side");
            case prism::Split::leftRight: return ray == 0 ? MixerWindow::tr ("left") : MixerWindow::tr ("right");
        }
        return {};
    }
}

//==============================================================================
// The return's Prism: its split along the top, then a row per ray (its name, its effects in
// the order they play, gain, mute, solo and a level bar).
class EffectPrismDialog final : public QDialog
{
public:
    EffectPrismDialog (MixerWindow& w, int r, std::shared_ptr<mixer::EffectPrism> p)
        : QDialog (&w), window (w), index (r), prism (std::move (p))
    {
        setAttribute (Qt::WA_DeleteOnClose);
        retitle();
        auto* layout = new QVBoxLayout (this);

        auto* head = new QHBoxLayout;
        head->addWidget (new QLabel (MixerWindow::tr ("Name"), this));
        nameEdit = new QLineEdit (QString::fromStdString (prism->name()), this);
        nameEdit->setAccessibleName (MixerWindow::tr ("Prism name"));
        QObject::connect (nameEdit, &QLineEdit::editingFinished, this, [this]
        {
            prism->setName (nameEdit->text().trimmed().toStdString());
            retitle();
            window.returnPrismChanged (index);
        });
        head->addWidget (nameEdit, 1);
        head->addSpacing (12);
        head->addWidget (new QLabel (MixerWindow::tr ("Split"), this));
        splitBox = new QComboBox (this);
        for (int s = 0; s < prism::numSplits; ++s)
            splitBox->addItem (splitLabel ((prism::Split) s));
        splitBox->setCurrentIndex ((int) prism->split());
        splitBox->setToolTip (MixerWindow::tr ("How the sound is divided among the rays: every ray hears all of it (Parallel), a band each "
                                               "(Bands, up to four), the mid and the side, or the left and the right"));
        QObject::connect (splitBox, &QComboBox::currentIndexChanged, this, [this] (int s)
        {
            prism->setSplit ((prism::Split) s);
            // Bands, Mid/Side and Left/Right bring the rays they need.
            const int wanted = s == (int) prism::Split::bands ? 3 : s == (int) prism::Split::parallel ? 1 : 2;
            if (prism->numRays() < wanted)
                edit ([&] (std::vector<FxRay>& list) { list.resize ((size_t) wanted); }, false);
            window.returnPrismChanged (index);
            rebuild();
        });
        head->addWidget (splitBox);
        for (int k = 0; k < 3; ++k)
        {
            auto* hz = new QDoubleSpinBox (this);
            hz->setRange (20.0, 20000.0);
            hz->setDecimals (0);
            hz->setSuffix (QStringLiteral (" Hz"));
            hz->setValue (prism->crossovers()[(size_t) k]);
            hz->setToolTip (MixerWindow::tr ("Crossover %1, between band %1 and band %2 (Linkwitz-Riley, 24 dB/octave)").arg (k + 1).arg (k + 2));
            QObject::connect (hz, &QDoubleSpinBox::valueChanged, this, [this, k] (double v)
            {
                auto now = prism->crossovers();
                now[(size_t) k] = v;
                prism->setCrossovers (now);
                window.returnPrismChanged (index);
            });
            crossovers[(size_t) k] = hz;
            head->addWidget (hz);
        }
        addRay = new QPushButton (MixerWindow::tr ("+ Add Ray"), this);
        addRay->setToolTip (MixerWindow::tr ("A new ray, empty until it is given effects (Ctrl+T)"));
        addRay->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_T));
        QObject::connect (addRay, &QPushButton::clicked, this, [this]
        {
            if (prism->numRays() < mixer::EffectPrism::maxRays)
                edit ([] (std::vector<FxRay>& list) { list.push_back ({}); }, true);
        });
        head->addWidget (addRay);
        layout->addLayout (head);

        auto* note = new QLabel (MixerWindow::tr ("What the tracks send to this return is divided among the rays; each ray plays its effects in "
                                                  "series, and the rays are joined again, in time with each other. An empty ray is its share "
                                                  "as it came in."), this);
        note->setWordWrap (true);
        note->setForegroundRole (QPalette::PlaceholderText);
        layout->addWidget (note);

        auto* scroll = new QScrollArea (this);
        scroll->setWidgetResizable (true);
        scroll->setFrameShape (QFrame::NoFrame);
        auto* host = new QWidget;
        raysLayout = new QVBoxLayout (host);
        raysLayout->setContentsMargins (0, 0, 0, 0);
        scroll->setWidget (host);
        layout->addWidget (scroll, 1);

        QObject::connect (&meterTimer, &QTimer::timeout, this, [this]
        {
            for (int i = 0; i < (int) meters.size(); ++i)
            {
                const auto peak = prism->takeRayPeak (i);
                const auto db = peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -60.0f;
                meters[(size_t) i]->setValue ((int) std::lround (std::clamp (db + 60.0f, 0.0f, 60.0f)));
            }
        });
        meterTimer.start (50);
        rebuild();
        resize (860, 380);
    }

private:
    void retitle()
    {
        setWindowTitle (MixerWindow::tr ("%1: %2").arg (MixerWindow::returnName (index), QString::fromStdString (prism->name())));
    }

    void edit (const std::function<void (std::vector<FxRay>&)>& change, bool rows)
    {
        auto list = prism->rays();
        change (list);
        prism->setRays (std::move (list));
        window.returnPrismChanged (index);
        if (rows)
            rebuild();
    }

    void rebuild()
    {
        meters.clear();
        while (auto* item = raysLayout->takeAt (0))
        {
            if (auto* w = item->widget())
                w->deleteLater();
            delete item;
        }
        const auto bands = prism->split() == prism::Split::bands;
        for (int k = 0; k < 3; ++k)
            crossovers[(size_t) k]->setVisible (bands && k + 1 < prism->raysHeard());
        const auto list = prism->rays();
        for (int i = 0; i < (int) list.size(); ++i)
            raysLayout->addWidget (row (i, list[(size_t) i]));
        if (list.empty())
        {
            auto* empty = new QLabel (MixerWindow::tr ("No rays: the sends pass through. + Add Ray starts one."), this);
            empty->setForegroundRole (QPalette::PlaceholderText);
            raysLayout->addWidget (empty);
        }
        raysLayout->addStretch (1);
        addRay->setEnabled ((int) list.size() < mixer::EffectPrism::maxRays);
    }

    QWidget* row (int i, const FxRay& ray)
    {
        const auto heard = prism->raysHeard();
        auto* frame = new QFrame (this);
        frame->setFrameShape (QFrame::StyledPanel);
        auto* v = new QVBoxLayout (frame);
        v->setContentsMargins (6, 4, 6, 4);

        auto* chain = new QHBoxLayout;
        chain->addWidget (new QLabel (QString::number (i + 1), frame));
        auto* name = new QLineEdit (QString::fromStdString (ray.name), frame);
        const auto role = rayRole (prism->split(), i, heard);
        name->setPlaceholderText (role.isEmpty() ? MixerWindow::tr ("Ray %1").arg (i + 1) : role);
        name->setFixedWidth (120);
        name->setAccessibleName (MixerWindow::tr ("Ray %1 name").arg (i + 1));
        QObject::connect (name, &QLineEdit::editingFinished, this, [this, i, name]
        {
            edit ([&] (std::vector<FxRay>& list) { if (i < (int) list.size()) list[(size_t) i].name = name->text().trimmed().toStdString(); }, false);
        });
        chain->addWidget (name);

        if (ray.effects.empty())
        {
            auto* dry = new QLabel (MixerWindow::tr ("(as it comes in)"), frame);
            dry->setForegroundRole (QPalette::PlaceholderText);
            chain->addWidget (dry);
        }
        for (int k = 0; k < (int) ray.effects.size(); ++k)
        {
            const auto& x = ray.effects[(size_t) k];
            if (k > 0)
                chain->addWidget (new QLabel (QString::fromUtf8 ("→"), frame));
            auto* fx = new QToolButton (frame);
            auto text = QString::fromStdString (x.effect->name());
            if (x.mix < 0.995f)
                text += QStringLiteral (" %1%").arg ((int) std::lround (x.mix * 100.0f));
            fx->setText (text + QString::fromUtf8 (" ▾"));
            auto f = fx->font();
            f.setStrikeOut (x.bypassed);
            fx->setFont (f);
            fx->setToolTip (x.bypassed ? MixerWindow::tr ("Bypassed: click for its menu") : MixerWindow::tr ("Click to open it, bypass, move or remove it"));
            QObject::connect (fx, &QToolButton::clicked, this, [this, i, k, fx] { effectMenu (i, k, fx->mapToGlobal (QPoint (0, fx->height()))); });
            chain->addWidget (fx);
        }
        auto* addFx = new QToolButton (frame);
        addFx->setText (MixerWindow::tr ("+ Effect"));
        addFx->setToolTip (MixerWindow::tr ("An effect at the end of this ray's chain: built-in, stock or a plug-in"));
        addFx->setEnabled ((int) ray.effects.size() < mixer::EffectPrism::maxEffects);
        QObject::connect (addFx, &QToolButton::clicked, this, [this, i, addFx]
        {
            QMenu menu (this);
            window.effectsMenu (menu, [this, i] (const std::string& id) { addEffect (i, id, -1); });
            menu.exec (addFx->mapToGlobal (QPoint (0, addFx->height())));
        });
        chain->addWidget (addFx);
        chain->addStretch (1);
        v->addLayout (chain);

        auto* controls = new QHBoxLayout;
        if (i >= heard)
        {
            auto* off = new QLabel (MixerWindow::tr ("Not heard: %1 feeds %2 rays").arg (splitLabel (prism->split())).arg (heard), frame);
            off->setForegroundRole (QPalette::PlaceholderText);
            controls->addWidget (off);
            controls->addSpacing (12);
        }
        controls->addWidget (new QLabel (MixerWindow::tr ("Gain"), frame));
        auto* gain = new QDoubleSpinBox (frame);
        gain->setRange (-60.0, 12.0);
        gain->setDecimals (1);
        gain->setSingleStep (0.5);
        gain->setSuffix (QStringLiteral (" dB"));
        gain->setSpecialValueText (MixerWindow::tr ("Off"));
        gain->setValue (ray.gainDb);
        QObject::connect (gain, &QDoubleSpinBox::valueChanged, this, [this, i] (double db)
        {
            edit ([&] (std::vector<FxRay>& list) { if (i < (int) list.size()) list[(size_t) i].gainDb = (float) db; }, false);
        });
        controls->addWidget (gain);
        auto toggle = [&] (const QString& text, const QString& tip, bool on, bool FxRay::*field)
        {
            auto* b = new QToolButton (frame);
            b->setText (text);
            b->setToolTip (tip);
            b->setCheckable (true);
            b->setChecked (on);
            QObject::connect (b, &QToolButton::toggled, this, [this, i, field] (bool now)
            {
                edit ([&] (std::vector<FxRay>& list) { if (i < (int) list.size()) list[(size_t) i].*field = now; }, false);
            });
            controls->addWidget (b);
        };
        toggle (QStringLiteral ("M"), MixerWindow::tr ("Mute the ray"), ray.muted, &FxRay::muted);
        toggle (QStringLiteral ("S"), MixerWindow::tr ("Solo the ray: hear only the soloed rays"), ray.soloed, &FxRay::soloed);
        auto* meter = new QProgressBar (frame);
        meter->setRange (0, 60);
        meter->setTextVisible (false);
        meter->setFixedSize (70, 10);
        meter->setToolTip (MixerWindow::tr ("The ray's level (60 dB)"));
        meters.push_back (meter);
        controls->addWidget (meter);
        controls->addStretch (1);

        auto button = [&] (const char* text, const QString& tip, bool enabled, std::function<void()> act)
        {
            auto* b = new QToolButton (frame);
            b->setText (QString::fromUtf8 (text));
            b->setToolTip (tip);
            b->setEnabled (enabled);
            QObject::connect (b, &QToolButton::clicked, this, std::move (act));
            controls->addWidget (b);
        };
        button ("↑", MixerWindow::tr ("Move the ray up"), i > 0, [this, i]
        {
            edit ([&] (std::vector<FxRay>& list) { if (i > 0 && i < (int) list.size()) std::swap (list[(size_t) i], list[(size_t) i - 1]); }, true);
        });
        button ("↓", MixerWindow::tr ("Move the ray down"), i + 1 < prism->numRays(), [this, i]
        {
            edit ([&] (std::vector<FxRay>& list) { if (i + 1 < (int) list.size()) std::swap (list[(size_t) i], list[(size_t) i + 1]); }, true);
        });
        button ("✕", MixerWindow::tr ("Remove the ray and its effects"), true, [this, i]
        {
            edit ([&] (std::vector<FxRay>& list) { if (i < (int) list.size()) list.erase (list.begin() + i); }, true);
        });
        v->addLayout (controls);
        return frame;
    }

    void addEffect (int i, const std::string& id, int replace)
    {
        std::string error;
        auto made = window.audio.loadEffectPlugin (id, window.rate(), error);
        if (made == nullptr)
        {
            QMessageBox::warning (this, MixerWindow::tr ("Prism"), MixerWindow::tr ("The effect could not be started: %1").arg (QString::fromStdString (error)));
            return;
        }
        edit ([&] (std::vector<FxRay>& list)
        {
            if (i >= (int) list.size())
                return;
            auto& fx = list[(size_t) i].effects;
            if (replace >= 0 && replace < (int) fx.size())
                fx[(size_t) replace] = { made, false, 1.0f };
            else if ((int) fx.size() < mixer::EffectPrism::maxEffects)
                fx.push_back ({ made, false, 1.0f });
        }, true);
    }

    void effectMenu (int i, int k, QPoint at)
    {
        const auto list = prism->rays();
        if (i >= (int) list.size() || k >= (int) list[(size_t) i].effects.size())
            return;
        const auto x = list[(size_t) i].effects[(size_t) k];
        const auto count = (int) list[(size_t) i].effects.size();
        QMenu menu (this);
        menu.addAction (MixerWindow::tr ("&Open"), this, [this, x, i]
        {
            if (x.effect->hasWindow())
                x.effect->showWindow();
            else
                effectDialog (x.effect, MixerWindow::tr ("%1, ray %2: %3").arg (MixerWindow::returnName (index)).arg (i + 1).arg (QString::fromStdString (x.effect->name())),
                              this, [this] { window.changed(); })->show();
        });
        auto* bypass = menu.addAction (MixerWindow::tr ("&Bypass"));
        bypass->setCheckable (true);
        bypass->setChecked (x.bypassed);
        QObject::connect (bypass, &QAction::toggled, this, [this, i, k] (bool on)
        {
            edit ([&] (std::vector<FxRay>& rays) { if (i < (int) rays.size() && k < (int) rays[(size_t) i].effects.size()) rays[(size_t) i].effects[(size_t) k].bypassed = on; }, true);
        });
        menu.addAction (MixerWindow::tr ("&Mix..."), this, [this, i, k, x]
        {
            bool ok = false;
            const auto mix = QInputDialog::getInt (this, MixerWindow::tr ("Mix"), MixerWindow::tr ("How much of %1 is heard against the sound going in (%):").arg (QString::fromStdString (x.effect->name())),
                                                   (int) std::lround (x.mix * 100.0f), 0, 100, 1, &ok);
            if (ok)
                edit ([&] (std::vector<FxRay>& rays) { if (i < (int) rays.size() && k < (int) rays[(size_t) i].effects.size()) rays[(size_t) i].effects[(size_t) k].mix = (float) mix / 100.0f; }, true);
        });
        menu.addSeparator();
        auto move = [this, i, k] (int to)
        {
            edit ([&] (std::vector<FxRay>& rays)
            {
                if (i >= (int) rays.size())
                    return;
                auto& fx = rays[(size_t) i].effects;
                if (k < (int) fx.size() && to >= 0 && to < (int) fx.size())
                    std::swap (fx[(size_t) k], fx[(size_t) to]);
            }, true);
        };
        menu.addAction (MixerWindow::tr ("Move &Earlier"), this, [move, k] { move (k - 1); })->setEnabled (k > 0);
        menu.addAction (MixerWindow::tr ("Move &Later"), this, [move, k] { move (k + 1); })->setEnabled (k + 1 < count);
        auto* replace = menu.addMenu (MixerWindow::tr ("Re&place With"));
        window.effectsMenu (*replace, [this, i, k] (const std::string& id) { addEffect (i, id, k); });
        menu.addSeparator();
        menu.addAction (MixerWindow::tr ("&Remove"), this, [this, i, k]
        {
            edit ([&] (std::vector<FxRay>& rays)
            {
                if (i < (int) rays.size() && k < (int) rays[(size_t) i].effects.size())
                    rays[(size_t) i].effects.erase (rays[(size_t) i].effects.begin() + k);
            }, true);
        });
        menu.exec (at);
    }

    MixerWindow& window;
    const int index;
    std::shared_ptr<mixer::EffectPrism> prism;
    QLineEdit* nameEdit = nullptr;
    QComboBox* splitBox = nullptr;
    std::array<QDoubleSpinBox*, 3> crossovers {};
    QPushButton* addRay = nullptr;
    QVBoxLayout* raysLayout = nullptr;
    std::vector<QProgressBar*> meters;
    QTimer meterTimer;
};

//==============================================================================
void MixerWindow::makeReturnPrism (int index)
{
    const auto old = engine.returnEffect (index);
    if (old != nullptr && mixer::EffectPrism::is (*old))
    {
        openReturnPrism (index);
        return;
    }
    auto prism = std::make_shared<mixer::EffectPrism>();
    if (old != nullptr)
    {
        // The return's effect goes into the first ray, still running with its settings. It
        // leaves the return first, so the Prism prepares it while nothing else plays it.
        engine.setReturnEffect (index, nullptr);
        auto rays = prism->rays();
        rays.front().effects.push_back ({ old, false, 1.0f });
        rays.front().name = old->name();
        prism->setRays (std::move (rays));
    }
    engine.setReturnEffect (index, prism);
    returnPrismChanged (index);
    openReturnPrism (index);
}

void MixerWindow::openReturnPrism (int index)
{
    const auto fx = engine.returnEffect (index);
    if (fx == nullptr || ! mixer::EffectPrism::is (*fx))
        return;
    auto& open = returnPrismEditors[(size_t) index];
    if (open == nullptr)
        open = new EffectPrismDialog (*this, index, std::static_pointer_cast<mixer::EffectPrism> (fx));
    open->show();
    open->raise();
    open->activateWindow();
}

void MixerWindow::returnPrismChanged (int index)
{
    refreshReturn (index);
    changed();
}

QJsonObject MixerWindow::returnPrismJson (const mixer::Effect& fx) const
{
    const auto& prism = static_cast<const mixer::EffectPrism&> (fx);
    QJsonArray rays;
    for (const auto& r : prism.rays())
        rays.append (QJsonObject { { "effects", effectsJson (r.effects) } });
    return { { "id", QString::fromUtf8 (mixer::EffectPrism::identifier) }, { "name", QString::fromStdString (prism.name()) },
             { "state", QString::fromStdString (prism.state()) }, { "rays", rays } };
}

mixer::EffectPtr MixerWindow::returnPrismFromJson (const QJsonObject& o, int index, QStringList& problems)
{
    auto prism = std::make_shared<mixer::EffectPrism>();
    std::vector<mixer::EffectPrism::Ray> rays;
    for (const auto& value : o.value ("rays").toArray())
    {
        mixer::EffectPrism::Ray r;
        r.effects = effectsFromJson (value.toObject().value ("effects").toArray(),
                                     tr ("%1's ray %2").arg (returnName (index)).arg (rays.size() + 1), problems);
        rays.push_back (std::move (r));
    }
    prism->setRays (std::move (rays));
    prism->setState (o.value ("state").toString().toStdString());
    return prism;
}
