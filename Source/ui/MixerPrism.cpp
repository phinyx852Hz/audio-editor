// SPDX-License-Identifier: AGPL-3.0-only
// Instrument Prisms on the mixer's tracks (docs/prism.md): a track's instrument made of rays,
// each an instrument and then its own effects in series, played together. The track's inserts
// come after the Prism, so they process all of its rays.
#include "MixerWindow.h"
#include "../engine/AudioEngine.h"
#include "../mixer/InstrumentPrism.h"
#include "../mixer/RagaInstrument.h"

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
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    using Ray = mixer::InstrumentPrism::Ray;

    // A key, shown and typed as a note name (C4 is 60) or as its number.
    class NoteBox final : public QSpinBox
    {
    public:
        explicit NoteBox (QWidget* parent) : QSpinBox (parent)
        {
            setRange (0, 127);
            setAccelerated (true);
        }

    protected:
        QString textFromValue (int v) const override { return QString::fromStdString (mixer::prismNoteName (v)); }

        int valueFromText (const QString& text) const override
        {
            int note = 0;
            return parse (text, note) ? note : value();
        }

        QValidator::State validate (QString& text, int&) const override
        {
            int note = 0;
            return parse (text, note) ? QValidator::Acceptable : QValidator::Intermediate;
        }

    private:
        static bool parse (QString text, int& note)
        {
            text = text.trimmed();
            bool number = false;
            if (const auto n = text.toInt (&number); number)
            {
                note = n;
                return n >= 0 && n <= 127;
            }
            static const int steps[] { 9, 11, 0, 2, 4, 5, 7 };   // A to G
            if (text.isEmpty())
                return false;
            const auto letter = text[0].toUpper().unicode();
            if (letter < 'A' || letter > 'G')
                return false;
            int pitch = steps[letter - 'A'], at = 1;
            if (at < text.size() && (text[at] == QLatin1Char ('#') || text[at] == QChar (0x266F)))
                ++pitch, ++at;
            else if (at < text.size() && (text[at] == QLatin1Char ('b') || text[at] == QChar (0x266D)))
                --pitch, ++at;
            bool ok = false;
            const auto octave = text.mid (at).toInt (&ok);
            if (! ok)
                return false;
            note = (octave + 1) * 12 + pitch;
            return note >= 0 && note <= 127;
        }
    };

    QJsonObject rayInstrumentJson (const mixer::Instrument& inst)
    {
        switch (inst.kind())
        {
            case mixer::Instrument::Kind::synth: return { { "kind", "synth" }, { "state", QString::fromStdString (inst.state()) } };
            case mixer::Instrument::Kind::raga:  return { { "kind", "raga" }, { "state", QString::fromStdString (inst.state()) } };
            case mixer::Instrument::Kind::plugin:
                return { { "kind", "plugin" }, { "id", QString::fromStdString (AudioEngine::instrumentPluginId (inst)) },
                         { "name", QString::fromStdString (inst.name()) }, { "state", QString::fromStdString (inst.state()) } };
            case mixer::Instrument::Kind::sampler:
            case mixer::Instrument::Kind::slicer:
            case mixer::Instrument::Kind::prism:
                break;   // not in rays (yet)
        }
        return {};
    }
}

//==============================================================================
// The Prism's rays, a row each: its name, its instrument and then its effects (in the order
// they play), its key and velocity ranges, gain, mute and solo, and a level bar.
class PrismDialog final : public QDialog
{
public:
    PrismDialog (MixerWindow& w, int t, std::shared_ptr<mixer::InstrumentPrism> p)
        : QDialog (&w), window (w), track (t), prism (std::move (p))
    {
        setAttribute (Qt::WA_DeleteOnClose);
        setWindowTitle (MixerWindow::tr ("Track %1: %2").arg (track + 1).arg (QString::fromStdString (prism->name())));
        auto* layout = new QVBoxLayout (this);

        auto* head = new QHBoxLayout;
        head->addWidget (new QLabel (MixerWindow::tr ("Name"), this));
        nameEdit = new QLineEdit (QString::fromStdString (prism->name()), this);
        nameEdit->setAccessibleName (MixerWindow::tr ("Prism name"));
        QObject::connect (nameEdit, &QLineEdit::editingFinished, this, [this]
        {
            prism->setName (nameEdit->text().trimmed().toStdString());
            setWindowTitle (MixerWindow::tr ("Track %1: %2").arg (track + 1).arg (QString::fromStdString (prism->name())));
            window.prismChanged (track);
        });
        head->addWidget (nameEdit, 1);
        addRay = new QPushButton (MixerWindow::tr ("+ Add Ray"), this);
        addRay->setToolTip (MixerWindow::tr ("A new ray with a Synth, playing every note (Ctrl+T)"));
        addRay->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_T));
        QObject::connect (addRay, &QPushButton::clicked, this, [this] { addSynthRay(); });
        head->addWidget (addRay);
        layout->addLayout (head);

        auto* note = new QLabel (MixerWindow::tr ("Each ray plays its instrument and then its effects, in series; the rays play together. "
                                                  "A ray hears only the notes in its key and velocity ranges, to split or layer the keyboard. "
                                                  "The track's own effects, after the Prism in its Device Chain, process all of the rays."), this);
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
        resize (900, 420);
    }

private:
    // Edits the rays; `rows` rebuilds the rows after (rays added, removed, moved or replaced).
    void edit (const std::function<void (std::vector<Ray>&)>& change, bool rows)
    {
        auto list = prism->rays();
        change (list);
        prism->setRays (std::move (list));
        window.prismChanged (track);
        if (rows)
            rebuild();
    }

    void addSynthRay()
    {
        if (prism->numRays() >= mixer::InstrumentPrism::maxRays)
            return;
        Ray ray;
        ray.instrument = std::make_shared<mixer::Synth>();
        ray.name = "Synth";
        edit ([&] (std::vector<Ray>& list) { list.push_back (ray); }, true);
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
        const auto list = prism->rays();
        for (int i = 0; i < (int) list.size(); ++i)
            raysLayout->addWidget (row (i, list[(size_t) i]));
        if (list.empty())
        {
            auto* empty = new QLabel (MixerWindow::tr ("No rays: + Add Ray starts one."), this);
            empty->setForegroundRole (QPalette::PlaceholderText);
            raysLayout->addWidget (empty);
        }
        raysLayout->addStretch (1);
        addRay->setEnabled ((int) list.size() < mixer::InstrumentPrism::maxRays);
    }

    QWidget* row (int i, const Ray& ray)
    {
        auto* frame = new QFrame (this);
        frame->setFrameShape (QFrame::StyledPanel);
        auto* v = new QVBoxLayout (frame);
        v->setContentsMargins (6, 4, 6, 4);

        // Its chain: the instrument, then the effects.
        auto* chain = new QHBoxLayout;
        chain->addWidget (new QLabel (QString::number (i + 1), frame));
        auto* name = new QLineEdit (QString::fromStdString (ray.name), frame);
        name->setPlaceholderText (MixerWindow::tr ("Ray %1").arg (i + 1));
        name->setFixedWidth (120);
        name->setAccessibleName (MixerWindow::tr ("Ray %1 name").arg (i + 1));
        QObject::connect (name, &QLineEdit::editingFinished, this, [this, i, name]
        {
            edit ([&] (std::vector<Ray>& list) { if (i < (int) list.size()) list[(size_t) i].name = name->text().trimmed().toStdString(); }, false);
        });
        chain->addWidget (name);

        auto* instrument = new QToolButton (frame);
        instrument->setText ((ray.instrument != nullptr ? QString::fromStdString (ray.instrument->name()) : MixerWindow::tr ("No instrument"))
                             + QString::fromUtf8 (" ▾"));
        instrument->setToolTip (MixerWindow::tr ("The ray's instrument: choose another, or open it"));
        QObject::connect (instrument, &QToolButton::clicked, this, [this, i, instrument] { instrumentMenu (i, instrument->mapToGlobal (QPoint (0, instrument->height()))); });
        chain->addWidget (instrument);
        auto* open = new QToolButton (frame);
        open->setText (MixerWindow::tr ("Edit"));
        open->setEnabled (ray.instrument != nullptr && (ray.instrument->kind() != mixer::Instrument::Kind::plugin || ray.instrument->hasWindow()));
        QObject::connect (open, &QToolButton::clicked, this, [this, i] { openInstrument (i); });
        chain->addWidget (open);

        for (int k = 0; k < (int) ray.effects.size(); ++k)
        {
            const auto& x = ray.effects[(size_t) k];
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
        addFx->setToolTip (MixerWindow::tr ("An effect at the end of this ray's chain, heard only on this ray"));
        addFx->setEnabled ((int) ray.effects.size() < mixer::InstrumentPrism::maxEffects);
        QObject::connect (addFx, &QToolButton::clicked, this, [this, i, addFx]
        {
            QMenu menu (this);
            window.effectsMenu (menu, [this, i] (const std::string& id) { addEffect (i, id, -1); });
            menu.exec (addFx->mapToGlobal (QPoint (0, addFx->height())));
        });
        chain->addWidget (addFx);
        chain->addStretch (1);
        v->addLayout (chain);

        // How it is heard.
        auto* controls = new QHBoxLayout;
        auto range = [&] (const QString& label, QSpinBox* low, QSpinBox* high, int lo, int hi, int Ray::*lowField, int Ray::*highField)
        {
            controls->addWidget (new QLabel (label, frame));
            low->setValue (lo);
            high->setValue (hi);
            controls->addWidget (low);
            controls->addWidget (new QLabel (QString::fromUtf8 ("–"), frame));
            controls->addWidget (high);
            // The other end follows when one is moved past it.
            auto apply = [this, i, low, high, lowField, highField] (bool movedLow)
            {
                if (low->value() > high->value())
                {
                    const QSignalBlocker quiet (movedLow ? static_cast<QSpinBox*> (high) : low);
                    (movedLow ? high : low)->setValue (movedLow ? low->value() : high->value());
                }
                edit ([&] (std::vector<Ray>& list)
                {
                    if (i < (int) list.size())
                    {
                        list[(size_t) i].*lowField = low->value();
                        list[(size_t) i].*highField = high->value();
                    }
                }, false);
            };
            QObject::connect (low, &QSpinBox::valueChanged, this, [apply] { apply (true); });
            QObject::connect (high, &QSpinBox::valueChanged, this, [apply] { apply (false); });
        };
        auto* lowKey = new NoteBox (frame);
        auto* highKey = new NoteBox (frame);
        lowKey->setToolTip (MixerWindow::tr ("The lowest note it plays (type a name like C3, or a number)"));
        highKey->setToolTip (MixerWindow::tr ("The highest note it plays"));
        range (MixerWindow::tr ("Keys"), lowKey, highKey, ray.lowKey, ray.highKey, &Ray::lowKey, &Ray::highKey);
        auto* lowVelocity = new QSpinBox (frame);
        auto* highVelocity = new QSpinBox (frame);
        lowVelocity->setRange (1, 127);
        highVelocity->setRange (1, 127);
        lowVelocity->setToolTip (MixerWindow::tr ("The softest note it plays"));
        highVelocity->setToolTip (MixerWindow::tr ("The hardest note it plays"));
        controls->addSpacing (12);
        range (MixerWindow::tr ("Velocity"), lowVelocity, highVelocity, ray.lowVelocity, ray.highVelocity, &Ray::lowVelocity, &Ray::highVelocity);

        controls->addSpacing (12);
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
            edit ([&] (std::vector<Ray>& list) { if (i < (int) list.size()) list[(size_t) i].gainDb = (float) db; }, false);
        });
        controls->addWidget (gain);
        auto toggle = [&] (const QString& text, const QString& tip, bool on, bool Ray::*field)
        {
            auto* b = new QToolButton (frame);
            b->setText (text);
            b->setToolTip (tip);
            b->setCheckable (true);
            b->setChecked (on);
            QObject::connect (b, &QToolButton::toggled, this, [this, i, field] (bool now)
            {
                edit ([&] (std::vector<Ray>& list) { if (i < (int) list.size()) list[(size_t) i].*field = now; }, false);
            });
            controls->addWidget (b);
        };
        toggle (QStringLiteral ("M"), MixerWindow::tr ("Mute the ray"), ray.muted, &Ray::muted);
        toggle (QStringLiteral ("S"), MixerWindow::tr ("Solo the ray: hear only the soloed rays"), ray.soloed, &Ray::soloed);
        auto* meter = new QProgressBar (frame);
        meter->setRange (0, 60);
        meter->setTextVisible (false);
        meter->setFixedSize (70, 10);
        meter->setToolTip (MixerWindow::tr ("The ray's level (60 dB)"));
        meters.push_back (meter);
        controls->addWidget (meter);
        controls->addStretch (1);

        auto* up = new QToolButton (frame);
        up->setText (QString::fromUtf8 ("↑"));
        up->setToolTip (MixerWindow::tr ("Move the ray up"));
        up->setEnabled (i > 0);
        QObject::connect (up, &QToolButton::clicked, this, [this, i]
        {
            edit ([&] (std::vector<Ray>& list) { if (i > 0 && i < (int) list.size()) std::swap (list[(size_t) i], list[(size_t) i - 1]); }, true);
        });
        controls->addWidget (up);
        auto* down = new QToolButton (frame);
        down->setText (QString::fromUtf8 ("↓"));
        down->setToolTip (MixerWindow::tr ("Move the ray down"));
        down->setEnabled (i + 1 < prism->numRays());
        QObject::connect (down, &QToolButton::clicked, this, [this, i]
        {
            edit ([&] (std::vector<Ray>& list) { if (i + 1 < (int) list.size()) std::swap (list[(size_t) i], list[(size_t) i + 1]); }, true);
        });
        controls->addWidget (down);
        auto* remove = new QToolButton (frame);
        remove->setText (QString::fromUtf8 ("✕"));
        remove->setToolTip (MixerWindow::tr ("Remove the ray, its instrument and its effects"));
        QObject::connect (remove, &QToolButton::clicked, this, [this, i]
        {
            closeEditorsOf (i);
            edit ([&] (std::vector<Ray>& list) { if (i < (int) list.size()) list.erase (list.begin() + i); }, true);
        });
        controls->addWidget (remove);
        v->addLayout (controls);
        return frame;
    }

    void instrumentMenu (int i, QPoint at)
    {
        QMenu menu (this);
        auto choose = [this, i] (const QByteArray& what)
        {
            return [this, i, what]
            {
                QString error;
                auto made = window.makeRayInstrument (what, error);
                if (made == nullptr)
                {
                    QMessageBox::warning (this, MixerWindow::tr ("Instrument Prism"), MixerWindow::tr ("The instrument could not be started: %1").arg (error));
                    return;
                }
                closeEditorsOf (i);
                edit ([&] (std::vector<Ray>& list)
                {
                    if (i >= (int) list.size())
                        return;
                    auto& r = list[(size_t) i];
                    // A ray named after its instrument takes the new one's name.
                    if (r.name.empty() || (r.instrument != nullptr && r.name == r.instrument->name()))
                        r.name = made->name();
                    r.instrument = made;
                }, true);
                if (made->kind() == mixer::Instrument::Kind::plugin && made->hasWindow())
                    made->showWindow();
            };
        };
        menu.addAction (MixerWindow::tr ("&Synth"), this, choose ("synth"));
        menu.addAction (QString::fromUtf8 ("PHI &rāga"), this, choose ("raga"));
        const auto plugins = window.audio.knownInstruments();
        if (! plugins.empty())
            menu.addSeparator();
        for (const auto& p : plugins)
            menu.addAction (QString::fromStdString (p.name) + QStringLiteral (" (") + QString::fromStdString (p.format) + QLatin1Char (')'),
                            this, choose ("instrument\n" + QByteArray::fromStdString (p.id)));
        menu.addSeparator();
        auto* later = menu.addAction (MixerWindow::tr ("Sampler and Slicer: on the track itself for now"));
        later->setEnabled (false);
        menu.exec (at);
    }

    void openInstrument (int i)
    {
        const auto list = prism->rays();
        if (i >= (int) list.size() || list[(size_t) i].instrument == nullptr)
            return;
        const auto inst = list[(size_t) i].instrument;
        if (auto& open = editors[inst.get()]; open != nullptr)
        {
            open->show();
            open->raise();
            open->activateWindow();
            return;
        }
        const auto title = MixerWindow::tr ("Track %1, ray %2").arg (track + 1).arg (QString::fromStdString (list[(size_t) i].name));
        QDialog* d = nullptr;
        switch (inst->kind())
        {
            case mixer::Instrument::Kind::synth: d = synthDialog (inst, title, this, [this] { window.changed(); }); break;
            case mixer::Instrument::Kind::raga:  d = ragaDialog (inst, title, this, [this] { window.changed(); }); break;
            case mixer::Instrument::Kind::plugin:
                inst->showWindow();
                window.changed();
                return;
            case mixer::Instrument::Kind::sampler:
            case mixer::Instrument::Kind::slicer:
            case mixer::Instrument::Kind::prism:
                return;   // not in rays
        }
        editors[inst.get()] = d;
        d->show();
    }

    void closeEditorsOf (int i)
    {
        const auto list = prism->rays();
        if (i >= (int) list.size() || list[(size_t) i].instrument == nullptr)
            return;
        if (auto it = editors.find (list[(size_t) i].instrument.get()); it != editors.end())
        {
            if (it->second != nullptr)
                it->second->close();
            editors.erase (it);
        }
    }

    void addEffect (int i, const std::string& id, int replace)
    {
        std::string error;
        auto made = window.audio.loadEffectPlugin (id, window.rate(), error);
        if (made == nullptr)
        {
            QMessageBox::warning (this, MixerWindow::tr ("Instrument Prism"), MixerWindow::tr ("The effect could not be started: %1").arg (QString::fromStdString (error)));
            return;
        }
        edit ([&] (std::vector<Ray>& list)
        {
            if (i >= (int) list.size())
                return;
            auto& fx = list[(size_t) i].effects;
            if (replace >= 0 && replace < (int) fx.size())
                fx[(size_t) replace] = { made, false, 1.0f };
            else if ((int) fx.size() < mixer::InstrumentPrism::maxEffects)
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
                effectDialog (x.effect, MixerWindow::tr ("Track %1, ray %2: %3").arg (track + 1).arg (i + 1).arg (QString::fromStdString (x.effect->name())),
                              this, [this] { window.changed(); })->show();
        });
        auto* bypass = menu.addAction (MixerWindow::tr ("&Bypass"));
        bypass->setCheckable (true);
        bypass->setChecked (x.bypassed);
        QObject::connect (bypass, &QAction::toggled, this, [this, i, k] (bool on)
        {
            edit ([&] (std::vector<Ray>& rays) { if (i < (int) rays.size() && k < (int) rays[(size_t) i].effects.size()) rays[(size_t) i].effects[(size_t) k].bypassed = on; }, true);
        });
        menu.addAction (MixerWindow::tr ("&Mix..."), this, [this, i, k, x]
        {
            bool ok = false;
            const auto mix = QInputDialog::getInt (this, MixerWindow::tr ("Mix"), MixerWindow::tr ("How much of %1 is heard against the sound going in (%):").arg (QString::fromStdString (x.effect->name())),
                                                   (int) std::lround (x.mix * 100.0f), 0, 100, 1, &ok);
            if (ok)
                edit ([&] (std::vector<Ray>& rays) { if (i < (int) rays.size() && k < (int) rays[(size_t) i].effects.size()) rays[(size_t) i].effects[(size_t) k].mix = (float) mix / 100.0f; }, true);
        });
        menu.addSeparator();
        auto move = [this, i, k] (int to)
        {
            edit ([&] (std::vector<Ray>& rays)
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
            edit ([&] (std::vector<Ray>& rays)
            {
                if (i < (int) rays.size() && k < (int) rays[(size_t) i].effects.size())
                    rays[(size_t) i].effects.erase (rays[(size_t) i].effects.begin() + k);
            }, true);
        });
        menu.exec (at);
    }

    MixerWindow& window;
    const int track;
    std::shared_ptr<mixer::InstrumentPrism> prism;
    QLineEdit* nameEdit = nullptr;
    QPushButton* addRay = nullptr;
    QVBoxLayout* raysLayout = nullptr;
    std::vector<QProgressBar*> meters;
    QTimer meterTimer;
    std::map<const void*, QPointer<QDialog>> editors;   // a ray instrument's settings, by the instrument
};

//==============================================================================
bool MixerWindow::canBeRay (const mixer::Instrument& inst)
{
    const auto k = inst.kind();
    return k == mixer::Instrument::Kind::synth || k == mixer::Instrument::Kind::raga || k == mixer::Instrument::Kind::plugin;
}

mixer::InstrumentPtr MixerWindow::makeRayInstrument (const QByteArray& what, QString& error)
{
    const auto parts = QString::fromUtf8 (what).split (QLatin1Char ('\n'));
    const auto kind = parts.value (0);
    if (kind == QLatin1String ("synth"))
        return std::make_shared<mixer::Synth>();
    if (kind == QLatin1String ("raga"))
        return std::make_shared<mixer::RagaInstrument>();
    if (kind == QLatin1String ("instrument"))
    {
        std::string why;
        auto made = audio.loadInstrumentPlugin (parts.value (1).toStdString(), rate(), why);
        if (made == nullptr)
            error = QString::fromStdString (why);
        return made;
    }
    error = tr ("a ray cannot hold that");
    return nullptr;
}

mixer::InstrumentPtr MixerWindow::makePrism (int track)
{
    const auto old = engine.instrument (track);
    if (old != nullptr && old->kind() == mixer::Instrument::Kind::prism)
        return old;
    auto prism = std::make_shared<mixer::InstrumentPrism>();
    Ray ray;
    if (old != nullptr && canBeRay (*old))
    {
        // The track's instrument goes into the first ray, still playing with its settings. It
        // leaves the track first, so the Prism prepares it while nothing else plays it.
        engine.setInstrument (track, nullptr);
        ray.instrument = old;
    }
    else
        ray.instrument = std::make_shared<mixer::Synth>();
    ray.name = ray.instrument->name();
    prism->setRays ({ ray });
    return prism;
}

void MixerWindow::openPrism (int track)
{
    const auto inst = engine.instrument (track);
    if (inst == nullptr || inst->kind() != mixer::Instrument::Kind::prism)
        return;
    auto& t = info[(size_t) track];
    if (t.editor == nullptr)
        t.editor = new PrismDialog (*this, track, std::static_pointer_cast<mixer::InstrumentPrism> (inst));
    t.editor->show();
    t.editor->raise();
    t.editor->activateWindow();
}

QJsonObject MixerWindow::prismJson (const mixer::Instrument& inst) const
{
    const auto& prism = static_cast<const mixer::InstrumentPrism&> (inst);
    QJsonArray rays;
    for (const auto& r : prism.rays())
    {
        QJsonObject o {
            { "name", QString::fromStdString (r.name) }, { "gainDb", r.gainDb }, { "muted", r.muted }, { "soloed", r.soloed },
            { "keys", QJsonArray { r.lowKey, r.highKey } }, { "velocities", QJsonArray { r.lowVelocity, r.highVelocity } },
            { "effects", effectsJson (r.effects) },
        };
        if (r.instrument != nullptr)
            o.insert ("instrument", rayInstrumentJson (*r.instrument));
        rays.append (o);
    }
    return { { "kind", "prism" }, { "name", QString::fromStdString (prism.name()) }, { "rays", rays } };
}

mixer::InstrumentPtr MixerWindow::prismFromJson (const QJsonObject& o, int track, QStringList& problems)
{
    auto prism = std::make_shared<mixer::InstrumentPrism>();
    prism->setName (o.value ("name").toString().toStdString());
    std::vector<Ray> rays;
    for (const auto& value : o.value ("rays").toArray())
    {
        const auto ro = value.toObject();
        Ray r;
        r.name = ro.value ("name").toString().toStdString();
        const auto owner = tr ("Track %1's ray %2").arg (track + 1).arg (rays.size() + 1);
        const auto io = ro.value ("instrument").toObject();
        const auto kind = io.value ("kind").toString();
        if (kind == QLatin1String ("synth"))
            r.instrument = std::make_shared<mixer::Synth>();
        else if (kind == QLatin1String ("raga"))
            r.instrument = std::make_shared<mixer::RagaInstrument>();
        else if (kind == QLatin1String ("plugin"))
        {
            std::string why;
            r.instrument = audio.loadInstrumentPlugin (io.value ("id").toString().toStdString(), rate(), why);
            if (r.instrument == nullptr)
                problems << tr ("%1: %2 could not be started (%3)").arg (owner, io.value ("name").toString(), QString::fromStdString (why));
        }
        if (r.instrument != nullptr)
            r.instrument->setState (io.value ("state").toString().toStdString());
        r.effects = effectsFromJson (ro.value ("effects").toArray(), owner, problems);
        r.gainDb = (float) ro.value ("gainDb").toDouble (0.0);
        r.muted = ro.value ("muted").toBool();
        r.soloed = ro.value ("soloed").toBool();
        const auto keys = ro.value ("keys").toArray(), velocities = ro.value ("velocities").toArray();
        r.lowKey = keys.at (0).toInt (0);
        r.highKey = keys.at (1).toInt (127);
        r.lowVelocity = velocities.at (0).toInt (1);
        r.highVelocity = velocities.at (1).toInt (127);
        rays.push_back (std::move (r));
    }
    prism->setRays (std::move (rays));
    return prism;
}
