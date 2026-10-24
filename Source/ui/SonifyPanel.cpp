// SPDX-License-Identifier: AGPL-3.0-only
#include "SonifyPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    const char* const pitchClasses[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // A MIDI note shown by name: 60 is C4.
    class NoteSpin final : public QSpinBox
    {
    public:
        explicit NoteSpin (QWidget* parent) : QSpinBox (parent) { setRange (0, 127); }

    protected:
        QString textFromValue (int v) const override
        {
            return QString::fromLatin1 (pitchClasses[v % 12]) + QString::number (v / 12 - 1);
        }

        int valueFromText (const QString& text) const override { return parse (text); }

        QValidator::State validate (QString& text, int&) const override
        {
            return parse (text) >= 0 ? QValidator::Acceptable : QValidator::Intermediate;
        }

    private:
        // "C4", "f#2", "60"; -1 when it is none of them.
        static int parse (const QString& text)
        {
            const auto t = text.trimmed().toUpper();
            bool ok = false;
            for (int pc = 11; pc >= 0; --pc)   // C# before C
                if (t.startsWith (QLatin1String (pitchClasses[pc])))
                {
                    const int octave = t.mid ((int) std::strlen (pitchClasses[pc])).toInt (&ok);
                    const int n = (octave + 1) * 12 + pc;
                    return ok && n >= 0 && n <= 127 ? n : -1;
                }
            const int n = t.toInt (&ok);
            return ok && n >= 0 && n <= 127 ? n : -1;
        }
    };

    QLabel* label (const QString& text, QWidget* buddy, QWidget* parent)
    {
        auto* l = new QLabel (text, parent);
        l->setBuddy (buddy);
        return l;
    }
}

SonifyPanel::SonifyPanel (QWidget* parent, bool webPage) : QWidget (parent)
{
    setObjectName (QStringLiteral ("sonifyPanel"));

    play = new QPushButton (tr ("Play"), this);
    play->setCheckable (true);
    play->setFocusPolicy (Qt::NoFocus);
    play->setToolTip (tr ("Plays what is shown as notes (Visualizer > Sonify > Sonify the Visualizer)"));
    connect (play, &QPushButton::toggled, this, [this] (bool on)
    {
        play->setText (on ? tr ("Stop") : tr ("Play"));
        if (! quiet)
            Q_EMIT playToggled (on);
    });

    sources = new QComboBox (this);
    sources->addItem (tr ("The visualizer"), (int) visualizer);
    sources->addItem (tr ("Picture or video file..."), (int) file);
    sources->addItem (tr ("Camera"), (int) camera);
    if (webPage)
        sources->addItem (tr ("Web page behind PHINYX"), (int) web);
    sources->setToolTip (tr ("What is played: whichever view the visualizer shows (MilkDrop too), a picture or video shown in its "
                             "Picture view, a camera, or the web page behind PHINYX"));
    connect (sources, &QComboBox::activated, this, [this] (int row) { Q_EMIT sourceChosen (sources->itemData (row).toInt()); });

    mode = new QComboBox (this);
    mode->addItem (tr ("Scan"), (int) sonify::Mode::scan);
    mode->addItem (tr ("Lanes"), (int) sonify::Mode::lanes);
    mode->addItem (tr ("Spiral"), (int) sonify::Mode::spiral);
    mode->setToolTip (tr ("Scan: a line sweeps across, a column a step, and plays the bright spots it crosses (best for still pictures).\n"
                          "Lanes: a lane per voice, each playing where its light is and gliding as it moves (best for moving pictures).\n"
                          "Spiral: a step looks at the next point of a sunflower spiral, turning by the golden angle (137.5°), "
                          "so a still picture plays as a melody that never repeats."));

    targets = new QComboBox (this);
    targets->addItem (tr ("Built-in Synth"), (int) builtInSynth);
    targets->addItem (tr ("What the MIDI keyboard plays"), (int) keyboard);
    targets->setToolTip (tr ("Built-in Synth: a patchbay synth with General MIDI drums on channel 10.\n"
                             "What the MIDI keyboard plays: Simplest, the Slicer, the Mixer's armed track or the synth opened last "
                             "(Wavetable, FM, Modular...). Each voice has a MIDI channel of its own (1 to 8), as in MPE."));

    drums = new QCheckBox (tr ("Drums"), this);
    drums->setToolTip (tr ("Change strikes General MIDI drums on channel 10: kick at the bottom, snare in the middle, hi-hat at the top"));
    fibonacci = new QCheckBox (tr ("Φ rhythm"), this);
    fibonacci->setToolTip (tr ("The drums play only on the 1s of the Fibonacci word (1011010110110...): a groove that never quite repeats"));

    quantize = new QSlider (Qt::Horizontal, this);
    quantize->setRange (0, 100);
    quantize->setMinimumWidth (120);
    quantize->setToolTip (tr ("From the picture's own pitch, between the notes (by pitch bend), to the notes of the scale"));
    scale = new QComboBox (this);
    for (int i = 0; i < (int) sonify::Scale::count; ++i)
        scale->addItem (tr (sonify::scaleName ((sonify::Scale) i)), i);
    key = new QComboBox (this);
    for (const auto* pc : pitchClasses)
        key->addItem (QString::fromLatin1 (pc));
    bend = new QSpinBox (this);
    bend->setRange (1, 48);
    bend->setPrefix (QStringLiteral ("±"));
    bend->setSuffix (tr (" st"));
    bend->setToolTip (tr ("The instrument's pitch bend range in semitones (the built-in Synth's is 2), so free pitches land where they should"));

    tempo = new QSpinBox (this);
    tempo->setRange (20, 400);
    tempo->setSuffix (tr (" BPM"));
    tempo->setToolTip (tr ("A step is a sixteenth note at this tempo"));
    voices = new QSpinBox (this);
    voices->setRange (1, sonify::maxVoices);
    voices->setToolTip (tr ("How many notes at once (lanes: how many lanes)"));
    columns = new QSpinBox (this);
    columns->setRange (2, 128);
    columns->setToolTip (tr ("Scan: the steps across the picture. Spiral: eight times this many points before it starts again at the centre"));
    low = new NoteSpin (this);
    low->setToolTip (tr ("The note at the bottom of the picture"));
    high = new NoteSpin (this);
    high->setToolTip (tr ("The note at the top of the picture"));
    threshold = new QSlider (Qt::Horizontal, this);
    threshold->setRange (0, 95);
    threshold->setMinimumWidth (90);
    threshold->setToolTip (tr ("How bright a spot must be to sound: higher plays fewer notes"));

    note = new QLabel (this);
    note->setTextInteractionFlags (Qt::TextSelectableByMouse);

    auto* first = new QHBoxLayout;
    first->addWidget (play);
    first->addWidget (label (tr ("S&ource:"), sources, this));
    first->addWidget (sources);
    first->addWidget (label (tr ("&Mode:"), mode, this));
    first->addWidget (mode);
    first->addWidget (label (tr ("Pla&ys:"), targets, this));
    first->addWidget (targets);
    first->addWidget (drums);
    first->addWidget (fibonacci);
    first->addWidget (note, 1);

    auto* second = new QHBoxLayout;
    second->addWidget (label (tr ("Pitch: free"), quantize, this));
    second->addWidget (quantize, 1);
    second->addWidget (new QLabel (tr ("scale"), this));
    second->addSpacing (8);
    second->addWidget (label (tr ("Sc&ale:"), scale, this));
    second->addWidget (scale);
    second->addWidget (label (tr ("&Key:"), key, this));
    second->addWidget (key);
    second->addWidget (label (tr ("&Bend:"), bend, this));
    second->addWidget (bend);
    second->addStretch();

    auto* third = new QHBoxLayout;
    third->addWidget (label (tr ("&Tempo:"), tempo, this));
    third->addWidget (tempo);
    third->addWidget (label (tr ("&Voices:"), voices, this));
    third->addWidget (voices);
    third->addWidget (label (tr ("Ste&ps:"), columns, this));
    third->addWidget (columns);
    third->addWidget (label (tr ("&Notes:"), low, this));
    third->addWidget (low);
    third->addWidget (new QLabel (QStringLiteral ("–"), this));
    third->addWidget (high);
    third->addWidget (label (tr ("&Light:"), threshold, this));
    third->addWidget (threshold);
    third->addStretch();

    auto* rows = new QVBoxLayout (this);
    rows->setContentsMargins (8, 2, 8, 6);
    rows->setSpacing (4);
    rows->addLayout (first);
    rows->addLayout (second);
    rows->addLayout (third);

    const auto emitChanged = [this] { if (! quiet) Q_EMIT changed(); };
    for (auto* c : { mode, scale, key, targets })
        connect (c, &QComboBox::currentIndexChanged, this, emitChanged);
    for (auto* s : { quantize, threshold })
        connect (s, &QSlider::valueChanged, this, emitChanged);
    for (auto* s : { tempo, voices, columns, low, high, bend })
        connect (s, &QSpinBox::valueChanged, this, emitChanged);
    for (auto* c : { drums, fibonacci })
        connect (c, &QCheckBox::toggled, this, emitChanged);
    connect (mode, &QComboBox::currentIndexChanged, this, [this] { columns->setEnabled (mode->currentData().toInt() != (int) sonify::Mode::lanes); });

    setSettings ({});
}

sonify::Settings SonifyPanel::settings() const
{
    sonify::Settings s;
    s.mode = (sonify::Mode) mode->currentData().toInt();
    s.quantize = quantize->value() / 100.0;
    s.scale = (sonify::Scale) scale->currentData().toInt();
    s.root = key->currentIndex();
    s.lowest = std::min (low->value(), high->value() - 1);
    s.highest = high->value();
    s.bpm = tempo->value();
    s.columns = columns->value();
    s.voices = voices->value();
    s.threshold = threshold->value() / 100.0;
    s.drums = drums->isChecked();
    s.fibonacciDrums = fibonacci->isChecked();
    s.bendRange = bend->value();
    return s;
}

void SonifyPanel::setSettings (const sonify::Settings& s)
{
    quiet = true;
    mode->setCurrentIndex (mode->findData ((int) s.mode));
    quantize->setValue ((int) std::lround (s.quantize * 100.0));
    scale->setCurrentIndex (scale->findData ((int) s.scale));
    key->setCurrentIndex (std::clamp (s.root, 0, 11));
    low->setValue (s.lowest);
    high->setValue (s.highest);
    tempo->setValue ((int) std::lround (s.bpm));
    columns->setValue (s.columns);
    voices->setValue (s.voices);
    threshold->setValue ((int) std::lround (s.threshold * 100.0));
    drums->setChecked (s.drums);
    fibonacci->setChecked (s.fibonacciDrums);
    bend->setValue ((int) std::lround (s.bendRange));
    columns->setEnabled (s.mode != sonify::Mode::lanes);
    quiet = false;
}

int SonifyPanel::target() const { return targets->currentData().toInt(); }

void SonifyPanel::setTarget (int t)
{
    quiet = true;
    targets->setCurrentIndex (std::max (0, targets->findData (t)));
    quiet = false;
}

int SonifyPanel::source() const { return sources->currentData().toInt(); }

void SonifyPanel::setSource (int s) { sources->setCurrentIndex (std::max (0, sources->findData (s))); }

void SonifyPanel::setPlaying (bool on)
{
    quiet = true;
    play->setChecked (on);
    quiet = false;
}

void SonifyPanel::setNote (const QString& text) { note->setText (text); }
