// SPDX-License-Identifier: AGPL-3.0-only
#include "MixerWindow.h"
#include "SamplerWindow.h"
#include "Settings.h"
#include "SlicerWindow.h"
#include "Theme.h"
#include "../core/Dsp.h"
#include "../engine/AudioEngine.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDial>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int faderSteps = 1000;

    // The fader's travel: gain = 2 x position^4, so +6 dB at the top, 0 dB at 84 %
    // and most of the throw spent where mixes are made.
    float faderDb (int position)
    {
        if (position <= 0)
            return -100.0f;
        const auto p = (double) position / faderSteps;
        return (float) (20.0 * std::log10 (2.0 * p * p * p * p));
    }

    int faderPosition (double db)
    {
        if (db <= -96.0)
            return 0;
        return std::clamp ((int) std::lround (std::pow (std::pow (10.0, db / 20.0) / 2.0, 0.25) * faderSteps), 0, faderSteps);
    }

    QString dbText (double db)
    {
        if (db <= -96.0)
            return QStringLiteral ("-∞");
        return (db > 0.05 ? QStringLiteral ("+") : QString()) + QString::number (db, 'f', 1);
    }

    QString peakText (float linear)
    {
        return linear <= 1.0e-5f ? QStringLiteral ("-∞") : QString::number (20.0 * std::log10 (linear), 'f', 1);
    }

    QString panText (float pan)
    {
        const auto amount = (int) std::lround (std::abs (pan) * 100.0f);
        return amount == 0 ? QStringLiteral ("C") : QString::number (amount) + (pan < 0 ? QStringLiteral ("L") : QStringLiteral ("R"));
    }

    const QColor trackColours[mixer::maxTracks] {
        QColor (0x4f, 0xc3, 0xf7), QColor (0xef, 0x53, 0x50), QColor (0xab, 0x47, 0xbc), QColor (0x26, 0xa6, 0x9a),
        QColor (0xff, 0xb7, 0x4d), QColor (0x8d, 0x6e, 0x63), QColor (0x78, 0x90, 0x9c), QColor (0xec, 0x40, 0x7a),
    };

    bool isMidiFile (const QString& path)
    {
        const auto suffix = QFileInfo (path).suffix().toLower();
        return suffix == QLatin1String ("mid") || suffix == QLatin1String ("midi") || suffix == QLatin1String ("smf") || suffix == QLatin1String ("rmi");
    }

    QString audioFilter()
    {
        QStringList patterns;
        for (const auto& ext : AudioEngine::readableExtensions())
            patterns << QStringLiteral ("*.") + QString::fromStdString (ext);
        return MixerWindow::tr ("Audio files (%1);;All files (*)").arg (patterns.join (QLatin1Char (' ')));
    }

    AudioDataPtr soundAsAudio (const sampler::Sample& s, double rate)
    {
        AudioData::Builder b (2, rate);
        const float* ptrs[2] = { s.left.data(), s.right.data() };
        for (Frame at = 0; at < s.length(); at += 65536)
        {
            const auto n = (int) std::min<Frame> (65536, s.length() - at);
            const float* part[2] = { ptrs[0] + at, ptrs[1] + at };
            b.append (part, n);
        }
        return b.finish();
    }

    // The two instruments' settings in a mix file.
    QJsonObject samplerJson (const sampler::Settings& s)
    {
        return { { "mode", s.mode == sampler::Mode::oneShot ? "oneShot" : "classic" }, { "start", (double) s.start }, { "end", (double) s.end },
                 { "loop", s.loop }, { "loopStart", (double) s.loopStart }, { "loopEnd", (double) s.loopEnd }, { "root", s.rootNote },
                 { "transpose", s.transpose }, { "detune", s.detune }, { "gain", s.gainDb }, { "pan", s.pan }, { "velocity", s.velocity },
                 { "attack", s.attack }, { "decay", s.decay }, { "sustain", s.sustain }, { "release", s.release },
                 { "fadeIn", s.fadeIn }, { "fadeOut", s.fadeOut }, { "voices", s.voices } };
    }

    sampler::Settings samplerFromJson (const QJsonObject& o)
    {
        sampler::Settings s;
        s.mode = o.value ("mode").toString() == QLatin1String ("oneShot") ? sampler::Mode::oneShot : sampler::Mode::classic;
        s.start = (Frame) o.value ("start").toDouble (0);
        s.end = (Frame) o.value ("end").toDouble (-1);
        s.loop = o.value ("loop").toBool (true);
        s.loopStart = (Frame) o.value ("loopStart").toDouble (0);
        s.loopEnd = (Frame) o.value ("loopEnd").toDouble (-1);
        s.rootNote = o.value ("root").toInt (60);
        s.transpose = o.value ("transpose").toDouble (0);
        s.detune = o.value ("detune").toDouble (0);
        s.gainDb = o.value ("gain").toDouble (0);
        s.pan = o.value ("pan").toDouble (0);
        s.velocity = o.value ("velocity").toDouble (0.6);
        s.attack = o.value ("attack").toDouble (0.002);
        s.decay = o.value ("decay").toDouble (0.3);
        s.sustain = o.value ("sustain").toDouble (1.0);
        s.release = o.value ("release").toDouble (0.15);
        s.fadeIn = o.value ("fadeIn").toDouble (0);
        s.fadeOut = o.value ("fadeOut").toDouble (0.01);
        s.voices = o.value ("voices").toInt (8);
        return s;
    }

    QJsonObject kitJson (const slicer::Kit& k)
    {
        QJsonArray pads;
        for (int i = 0; i < slicer::numPads; ++i)
        {
            const auto& p = k.pads[(size_t) i];
            if (! p.used)
                continue;
            pads.append (QJsonObject { { "pad", i }, { "start", (double) p.start }, { "end", (double) p.end }, { "gain", p.gainDb },
                                       { "pan", p.pan }, { "pitch", p.pitch }, { "reverse", p.reverse }, { "gate", p.play == slicer::Play::gate },
                                       { "attack", p.attack }, { "release", p.release }, { "choke", p.choke } });
        }
        return { { "pads", pads }, { "velocity", k.velocity }, { "voices", k.voices },
                 { "layout", k.layout == slicer::Layout::push ? "push" : "chromatic" } };
    }

    slicer::Kit kitFromJson (const QJsonObject& o)
    {
        slicer::Kit k;
        for (const auto& v : o.value ("pads").toArray())
        {
            const auto p = v.toObject();
            const auto i = p.value ("pad").toInt (-1);
            if (i < 0 || i >= slicer::numPads)
                continue;
            auto& pad = k.pads[(size_t) i];
            pad.used = true;
            pad.start = (Frame) p.value ("start").toDouble();
            pad.end = (Frame) p.value ("end").toDouble();
            pad.gainDb = p.value ("gain").toDouble();
            pad.pan = p.value ("pan").toDouble();
            pad.pitch = p.value ("pitch").toDouble();
            pad.reverse = p.value ("reverse").toBool();
            pad.play = p.value ("gate").toBool() ? slicer::Play::gate : slicer::Play::oneShot;
            pad.attack = p.value ("attack").toDouble (0.0005);
            pad.release = p.value ("release").toDouble (0.03);
            pad.choke = p.value ("choke").toInt();
        }
        k.velocity = o.value ("velocity").toDouble (0.6);
        k.voices = o.value ("voices").toInt (16);
        k.layout = o.value ("layout").toString() == QLatin1String ("chromatic") ? slicer::Layout::chromatic : slicer::Layout::push;
        return k;
    }

    // Paths in a mix are kept relative to it, so a folder of mix and files can move.
    QString storedPath (const QString& file, const QDir& base)
    {
        if (file.isEmpty())
            return {};
        const auto relative = base.relativeFilePath (file);
        return relative.startsWith (QLatin1String ("../../")) ? QFileInfo (file).absoluteFilePath() : relative;
    }

    QString resolvedPath (const QString& stored, const QDir& base)
    {
        if (stored.isEmpty())
            return {};
        return QFileInfo (stored).isAbsolute() ? stored : QDir::cleanPath (base.absoluteFilePath (stored));
    }

    QString autosavePath()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/mixer/last.phinyxmix");
    }

    enum Choice { noInstrument, synthChoice, samplerChoice, slicerChoice, firstPlugin };
}

//==============================================================================
LevelMeter::LevelMeter (QWidget* parent) : QWidget (parent)
{
    setToolTip (MixerWindow::tr ("Peak (thin) and RMS (wide) for each side; the light on top shows a clip. Click to clear it"));
    setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void LevelMeter::setLevels (const mixer::Meter& m, double seconds)
{
    for (int c = 0; c < 2; ++c)
    {
        // The bar falls at 24 dB a second; the hold stays 1.5 seconds.
        const auto fall = (float) std::pow (10.0, -24.0 * seconds / 20.0);
        peak[c] = std::max (m.peak[c], peak[c] * fall);
        rms[c] = std::max (m.rms[c], rms[c] * fall);
        holdAge[c] += seconds;
        if (m.peak[c] >= hold[c] || holdAge[c] > 1.5)
        {
            hold[c] = m.peak[c];
            holdAge[c] = 0.0;
        }
        highest = std::max (highest, m.peak[c]);
        clipped = clipped || m.peak[c] >= 1.0f;
    }
    update();
}

void LevelMeter::clear()
{
    peak[0] = peak[1] = rms[0] = rms[1] = hold[0] = hold[1] = 0.0f;
    highest = 0.0f;
    clipped = false;
    update();
}

void LevelMeter::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto& theme = Theme::current();
    const auto light = 5;
    g.fillRect (rect(), theme.color (QStringLiteral ("meterBackground")));
    g.fillRect (QRect (0, 0, width(), light - 1), clipped ? theme.color (QStringLiteral ("meterHigh")) : theme.color (QStringLiteral ("mid")));
    const auto top = light, height = std::max (1, this->height() - light);
    // -60 dB at the bottom, 0 dB at the top.
    auto yFor = [&] (float level)
    {
        const auto db = level <= 1.0e-6f ? -60.0 : std::clamp (20.0 * std::log10 (level), -60.0, 0.0);
        return top + (int) std::lround ((1.0 - (db + 60.0) / 60.0) * height);
    };
    const auto half = width() / 2;
    for (int c = 0; c < 2; ++c)
    {
        const auto x = c * half, w = half - 1;
        const auto yPeak = yFor (peak[c]), yRms = yFor (rms[c]);
        const auto yMid = yFor (0.5f), yHigh = yFor (0.89f);   // -6 dB and -1 dB
        auto colourAt = [&] (int y) { return y <= yHigh ? QStringLiteral ("meterHigh") : (y <= yMid ? QStringLiteral ("meterMid") : QStringLiteral ("meterLow")); };
        for (int y = yPeak; y < top + height; ++y)
        {
            auto colour = theme.color (colourAt (y));
            if (y < yRms)
                colour.setAlpha (110);   // the peak above the RMS
            g.setPen (colour);
            g.drawLine (x, y, x + w - 1, y);
        }
        if (hold[c] > 1.0e-5f)
        {
            g.setPen (theme.color (QStringLiteral ("meterPeakHold")));
            const auto y = yFor (hold[c]);
            g.drawLine (x, y, x + w - 1, y);
        }
    }
}

void LevelMeter::mousePressEvent (QMouseEvent*)
{
    clipped = false;
    highest = 0.0f;
    update();
}

//==============================================================================
// One track's strip, top to bottom: name, the audio file, the MIDI file and its
// instrument, trim and polarity, pan, mute and solo, the fader and meter.
class MixerStrip final : public QFrame
{
public:
    MixerStrip (MixerWindow& w, int i) : QFrame (&w), window (w), index (i)
    {
        setFrameShape (QFrame::StyledPanel);
        setObjectName (QStringLiteral ("strip"));
        setAcceptDrops (true);
        setFocusPolicy (Qt::NoFocus);
        setMinimumWidth (128);
        setMaximumWidth (170);

        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (5, 5, 5, 5);
        layout->setSpacing (3);

        colourBar = new QFrame (this);
        colourBar->setFixedHeight (5);
        layout->addWidget (colourBar);
        number = new QLabel (QString::number (index + 1), this);
        nameEdit = new QLineEdit (this);
        nameEdit->setFrame (false);
        nameEdit->setToolTip (MixerWindow::tr ("The track's name"));
        auto* head = new QHBoxLayout;
        head->addWidget (number);
        head->addWidget (nameEdit, 1);
        layout->addLayout (head);

        auto smallButton = [this] (const QString& text, const QString& tip, bool checkable)
        {
            auto* b = new QPushButton (text, this);
            b->setFocusPolicy (Qt::NoFocus);
            b->setCheckable (checkable);
            b->setToolTip (tip);
            b->setMinimumWidth (10);
            return b;
        };

        // The audio file.
        audioOn = new QCheckBox (this);
        audioOn->setFocusPolicy (Qt::NoFocus);
        audioOn->setToolTip (MixerWindow::tr ("Hear the track's audio file"));
        audioButton = smallButton (MixerWindow::tr ("Audio..."), MixerWindow::tr ("Load an audio file onto the track (A), or drop one here. Right-click for more"), false);
        auto* audioRow = new QHBoxLayout;
        audioRow->addWidget (audioOn);
        audioRow->addWidget (audioButton, 1);
        layout->addLayout (audioRow);

        // The MIDI file, and what plays it.
        midiOn = new QCheckBox (this);
        midiOn->setFocusPolicy (Qt::NoFocus);
        midiOn->setToolTip (MixerWindow::tr ("Play the track's MIDI file into its instrument"));
        midiButton = smallButton (MixerWindow::tr ("MIDI..."), MixerWindow::tr ("Load a MIDI file (.mid) onto the track (I), or drop one here. It plays the instrument below; it is not edited"), false);
        auto* midiRow = new QHBoxLayout;
        midiRow->addWidget (midiOn);
        midiRow->addWidget (midiButton, 1);
        layout->addLayout (midiRow);

        instrumentBox = new QComboBox (this);
        instrumentBox->setFocusPolicy (Qt::NoFocus);
        instrumentBox->setToolTip (MixerWindow::tr ("What the MIDI file and the MIDI keyboard play: the built-in Synth (with drums on channel 10), "
                                                    "a Sampler or Slicer of the track's own, or an instrument plugin"));
        editButton = smallButton (MixerWindow::tr ("Edit"), MixerWindow::tr ("Open the instrument's window (E)"), false);
        auto* instRow = new QHBoxLayout;
        instRow->addWidget (instrumentBox, 1);
        instRow->addWidget (editButton);
        layout->addLayout (instRow);

        transposeBox = new QSpinBox (this);
        transposeBox->setRange (-48, 48);
        transposeBox->setSuffix (MixerWindow::tr (" st"));
        transposeBox->setToolTip (MixerWindow::tr ("Transpose the MIDI file (channel 10, the drums, stays)"));
        channelsButton = smallButton (MixerWindow::tr ("Ch"), MixerWindow::tr ("Which of the MIDI file's channels play"), false);
        armButton = smallButton (QStringLiteral ("●"), MixerWindow::tr ("The MIDI keyboard plays this track's instrument (R)"), true);
        auto* midiOptions = new QHBoxLayout;
        midiOptions->addWidget (transposeBox, 1);
        midiOptions->addWidget (channelsButton);
        midiOptions->addWidget (armButton);
        layout->addLayout (midiOptions);

        // Pre: trim and polarity.
        trimDial = new QDial (this);
        trimDial->setRange (-240, 240);
        trimDial->setFixedSize (40, 40);
        trimDial->setFocusPolicy (Qt::NoFocus);
        trimDial->setToolTip (MixerWindow::tr ("Trim, ±24 dB, before everything else (double-click: 0 dB)"));
        trimLabel = new QLabel (this);
        invertL = smallButton (QStringLiteral ("ØL"), MixerWindow::tr ("Flip the left side's polarity"), true);
        invertR = smallButton (QStringLiteral ("ØR"), MixerWindow::tr ("Flip the right side's polarity"), true);
        auto* pre = new QHBoxLayout;
        auto* trimColumn = new QVBoxLayout;
        trimColumn->addWidget (trimDial, 0, Qt::AlignHCenter);
        trimColumn->addWidget (trimLabel, 0, Qt::AlignHCenter);
        pre->addLayout (trimColumn);
        auto* polarity = new QVBoxLayout;
        polarity->addWidget (invertL);
        polarity->addWidget (invertR);
        pre->addLayout (polarity);
        layout->addLayout (pre);

        panDial = new QDial (this);
        panDial->setRange (-100, 100);
        panDial->setFixedSize (40, 40);
        panDial->setFocusPolicy (Qt::NoFocus);
        panDial->setToolTip (MixerWindow::tr ("Pan ([ and ]): equal power for a mono file, balance for stereo and instruments (double-click: centre)"));
        panLabel = new QLabel (this);
        auto* panColumn = new QVBoxLayout;
        panColumn->addWidget (panDial, 0, Qt::AlignHCenter);
        panColumn->addWidget (panLabel, 0, Qt::AlignHCenter);
        layout->addLayout (panColumn);

        muteButton = smallButton (MixerWindow::tr ("M"), MixerWindow::tr ("Mute (M)"), true);
        soloButton = smallButton (MixerWindow::tr ("S"), MixerWindow::tr ("Solo (S): soloed tracks are heard, the others are not"), true);
        auto* ms = new QHBoxLayout;
        ms->addWidget (muteButton);
        ms->addWidget (soloButton);
        layout->addLayout (ms);

        fader = new QSlider (Qt::Vertical, this);
        fader->setRange (0, faderSteps);
        fader->setFocusPolicy (Qt::NoFocus);
        fader->setToolTip (MixerWindow::tr ("Fader, -∞ to +6 dB (Up and Down, Shift for finer steps; 0 resets; double-click the value to type one)"));
        meter = new LevelMeter (this);
        auto* faderRow = new QHBoxLayout;
        faderRow->addStretch (1);
        faderRow->addWidget (fader);
        faderRow->addWidget (meter);
        faderRow->addStretch (1);
        layout->addLayout (faderRow, 1);
        faderValue = new QLabel (this);
        faderValue->setAlignment (Qt::AlignCenter);
        peakValue = new QLabel (this);
        peakValue->setAlignment (Qt::AlignCenter);
        peakValue->setToolTip (MixerWindow::tr ("The highest peak, in dB; click the meter to clear it"));
        auto* values = new QHBoxLayout;
        values->addWidget (faderValue);
        values->addWidget (peakValue);
        layout->addLayout (values);

        // Changes go straight to the engine.
        QObject::connect (nameEdit, &QLineEdit::textEdited, this, [this] (const QString& t)
        {
            window.info[(size_t) index].name = t;
            window.changed();
        });
        QObject::connect (nameEdit, &QLineEdit::editingFinished, &window, [this] { window.setFocus(); });
        QObject::connect (audioOn, &QCheckBox::toggled, this, [this] (bool on) { if (! updating) { strip().audioOn = on; window.changed(); } });
        QObject::connect (midiOn, &QCheckBox::toggled, this, [this] (bool on) { if (! updating) { strip().midiOn = on; window.changed(); } });
        QObject::connect (audioButton, &QPushButton::clicked, this, [this] { window.select (index); window.chooseAudio (index); });
        QObject::connect (midiButton, &QPushButton::clicked, this, [this] { window.select (index); window.chooseMidi (index); });
        QObject::connect (instrumentBox, &QComboBox::activated, this, [this] (int choice)
        {
            window.select (index);
            window.setInstrumentKind (index, std::min (choice, (int) firstPlugin), instrumentBox->itemData (choice).toString());
            window.setFocus();
        });
        QObject::connect (editButton, &QPushButton::clicked, this, [this] { window.editInstrument (index); });
        QObject::connect (transposeBox, &QSpinBox::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            strip().transpose = v;
            window.changed();
        });
        QObject::connect (transposeBox, &QSpinBox::editingFinished, &window, [this] { window.setFocus(); });
        QObject::connect (channelsButton, &QPushButton::clicked, this, [this] { channelMenu(); });
        QObject::connect (armButton, &QPushButton::clicked, this, [this] { window.arm (index); });
        QObject::connect (trimDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            strip().trimDb = (float) v / 10.0f;
            showValues();
            window.changed();
        });
        QObject::connect (invertL, &QPushButton::toggled, this, [this] (bool on) { if (! updating) { strip().invertLeft = on; window.changed(); } });
        QObject::connect (invertR, &QPushButton::toggled, this, [this] (bool on) { if (! updating) { strip().invertRight = on; window.changed(); } });
        QObject::connect (panDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            strip().pan = (float) v / 100.0f;
            showValues();
            window.changed();
        });
        QObject::connect (muteButton, &QPushButton::toggled, this, [this] (bool on) { if (! updating) { strip().mute = on; window.changed(); } });
        QObject::connect (soloButton, &QPushButton::toggled, this, [this] (bool on) { if (! updating) { strip().solo = on; window.changed(); } });
        QObject::connect (fader, &QSlider::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            strip().faderDb = faderDb (v);
            showValues();
            window.changed();
        });
        trimDial->installEventFilter (this);
        panDial->installEventFilter (this);
        faderValue->installEventFilter (this);
    }

    mixer::Strip& strip() { return window.engine.strip (index); }

    // Everything shown, from the engine and the window's track info.
    void refresh()
    {
        updating = true;
        auto& s = strip();
        const auto& info = window.info[(size_t) index];
        colourBar->setStyleSheet (QStringLiteral ("background: %1;").arg (info.colour.name()));
        if (nameEdit->text() != info.name)
            nameEdit->setText (info.name);

        const auto audio = window.engine.audio (index);
        const auto midi = window.engine.midi (index);
        auto fileName = [] (const std::string& p) { return QFileInfo (QString::fromStdString (p)).completeBaseName(); };
        audioButton->setText (info.busy && audio == nullptr ? MixerWindow::tr ("Loading...")
                              : audio != nullptr ? fileName (audio->path) : MixerWindow::tr ("Audio..."));
        audioButton->setToolTip (audio != nullptr ? QString::fromStdString (audio->path) + MixerWindow::tr ("\nClick for another file; right-click for more")
                                                  : MixerWindow::tr ("Load an audio file onto the track (A), or drop one here"));
        midiButton->setText (midi != nullptr ? fileName (midi->path) : MixerWindow::tr ("MIDI..."));
        if (midi != nullptr && midi->song != nullptr)
            midiButton->setToolTip (MixerWindow::tr ("%1\n%2 notes, %3 s at %4 BPM\nClick for another file; right-click for more")
                                        .arg (QString::fromStdString (midi->path)).arg (midi->song->noteCount())
                                        .arg (midi->song->lengthSeconds (midi->bpm), 0, 'f', 1)
                                        .arg (midi->bpm > 0.0 ? midi->bpm : midi->song->fileBpm(), 0, 'f', 1));
        else
            midiButton->setToolTip (MixerWindow::tr ("Load a MIDI file (.mid) onto the track (I), or drop one here. It plays the instrument below; it is not edited"));
        audioOn->setChecked (s.audioOn.load());
        audioOn->setEnabled (audio != nullptr);
        midiOn->setChecked (s.midiOn.load());
        midiOn->setEnabled (midi != nullptr);

        // The instrument list: built-ins, then the instrument plugins found by the scan.
        const auto plugins = window.audio.knownInstruments();
        if (instrumentBox->count() != firstPlugin + (int) plugins.size())
        {
            instrumentBox->clear();
            instrumentBox->addItems ({ MixerWindow::tr ("No instrument"), MixerWindow::tr ("Synth"), MixerWindow::tr ("Sampler"), MixerWindow::tr ("Slicer") });
            for (const auto& p : plugins)
                instrumentBox->addItem (QString::fromStdString (p.name) + QStringLiteral (" (") + QString::fromStdString (p.format) + QStringLiteral (")"),
                                        QString::fromStdString (p.id));
        }
        const auto inst = window.engine.instrument (index);
        int choice = noInstrument;
        if (inst != nullptr)
            switch (inst->kind())
            {
                case mixer::Instrument::Kind::synth:   choice = synthChoice; break;
                case mixer::Instrument::Kind::sampler: choice = samplerChoice; break;
                case mixer::Instrument::Kind::slicer:  choice = slicerChoice; break;
                case mixer::Instrument::Kind::plugin:
                {
                    choice = instrumentBox->findData (QString::fromStdString (AudioEngine::instrumentPluginId (*inst)));
                    if (choice < 0)
                    {
                        instrumentBox->addItem (QString::fromStdString (inst->name()), QString::fromStdString (AudioEngine::instrumentPluginId (*inst)));
                        choice = instrumentBox->count() - 1;
                    }
                    break;
                }
            }
        instrumentBox->setCurrentIndex (choice);
        editButton->setEnabled (inst != nullptr && (inst->kind() != mixer::Instrument::Kind::plugin || inst->hasWindow()));

        transposeBox->setValue (s.transpose.load());
        armButton->setChecked (window.engine.liveTrack() == index);
        trimDial->setValue ((int) std::lround (s.trimDb.load() * 10.0f));
        invertL->setChecked (s.invertLeft.load());
        invertR->setChecked (s.invertRight.load());
        panDial->setValue ((int) std::lround (s.pan.load() * 100.0f));
        muteButton->setChecked (s.mute.load());
        soloButton->setChecked (s.solo.load());
        fader->setValue (faderPosition (s.faderDb.load()));
        updating = false;
        showValues();
    }

    void showValues()
    {
        auto& s = strip();
        trimLabel->setText (dbText (s.trimDb.load()) + MixerWindow::tr (" dB"));
        panLabel->setText (panText (s.pan.load()));
        faderValue->setText (dbText (s.faderDb.load()));
    }

    void tick (double seconds)
    {
        meter->setLevels (window.engine.takeMeter (index), seconds);
        peakValue->setText (peakText (meter->highestPeak()));
    }

    void setSelected (bool on)
    {
        setStyleSheet (on ? QStringLiteral ("QFrame#strip { border: 2px solid %1; }").arg (Theme::current().color (QStringLiteral ("highlight")).name())
                          : QString());
        auto f = number->font();
        f.setBold (on);
        number->setFont (f);
    }

    void channelMenu()
    {
        QMenu menu (this);
        const auto midi = window.engine.midi (index);
        const auto used = midi != nullptr && midi->song != nullptr ? midi->song->channelsUsed : (uint16_t) 0xFFFF;
        const auto current = strip().channels.load();
        menu.addAction (MixerWindow::tr ("All Channels"), this, [this] { strip().channels = 0xFFFF; window.changed(); });
        menu.addAction (MixerWindow::tr ("No Drums (all but 10)"), this, [this] { strip().channels = 0xFFFF & ~(1u << 9); window.changed(); });
        menu.addAction (MixerWindow::tr ("Only Drums (10)"), this, [this] { strip().channels = 1u << 9; window.changed(); });
        menu.addSeparator();
        for (int ch = 0; ch < 16; ++ch)
        {
            auto text = MixerWindow::tr ("Channel %1").arg (ch + 1);
            if (ch == 9)
                text += MixerWindow::tr (" (drums)");
            if ((used & (1u << ch)) != 0 && midi != nullptr)
                text += MixerWindow::tr ("  • has notes");
            auto* a = menu.addAction (text);
            a->setCheckable (true);
            a->setChecked ((current & (1u << ch)) != 0);
            QObject::connect (a, &QAction::toggled, this, [this, ch] (bool on)
            {
                const auto mask = strip().channels.load();
                strip().channels = on ? (mask | (1u << ch)) : (mask & ~(1u << ch));
                window.changed();
            });
        }
        menu.exec (channelsButton->mapToGlobal (QPoint (0, channelsButton->height())));
    }

protected:
    bool eventFilter (QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::MouseButtonDblClick)
        {
            if (o == trimDial)
                trimDial->setValue (0);
            else if (o == panDial)
                panDial->setValue (0);
            else if (o == faderValue)
            {
                bool ok = false;
                const auto db = QInputDialog::getDouble (this, MixerWindow::tr ("Fader"), MixerWindow::tr ("Level (dB, -96 or less for silence):"),
                                                         std::max (-96.0, (double) strip().faderDb.load()), -100.0, 6.0, 1, &ok);
                if (ok)
                {
                    strip().faderDb = db <= -96.0 ? -100.0f : (float) db;
                    refresh();
                    window.changed();
                }
            }
            return true;
        }
        return QFrame::eventFilter (o, e);
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        window.select (index);
        QFrame::mousePressEvent (e);
    }

    void contextMenuEvent (QContextMenuEvent* e) override
    {
        window.select (index);
        QMenu menu (this);
        const auto audio = window.engine.audio (index);
        const auto midi = window.engine.midi (index);
        menu.addAction (MixerWindow::tr ("Load &Audio File..."), this, [this] { window.chooseAudio (index); });
        menu.addAction (MixerWindow::tr ("&Remove Audio File"), this, [this]
        {
            window.engine.setAudio (index, nullptr);
            refresh();
            window.changed();
        })->setEnabled (audio != nullptr);
        menu.addAction (MixerWindow::tr ("Audio &Start..."), this, [this] { offsetDialog (true); });
        menu.addSeparator();
        menu.addAction (MixerWindow::tr ("Load &MIDI File..."), this, [this] { window.chooseMidi (index); });
        menu.addAction (MixerWindow::tr ("R&emove MIDI File"), this, [this]
        {
            window.engine.setMidi (index, nullptr);
            refresh();
            window.changed();
        })->setEnabled (midi != nullptr);
        menu.addAction (MixerWindow::tr ("MIDI S&tart..."), this, [this] { offsetDialog (false); });
        menu.addSeparator();
        const auto inst = window.engine.instrument (index);
        const bool sampled = inst != nullptr && (inst->kind() == mixer::Instrument::Kind::sampler || inst->kind() == mixer::Instrument::Kind::slicer);
        menu.addAction (MixerWindow::tr ("Load Sound into the &Instrument..."), this, [this]
        {
            const auto file = QFileDialog::getOpenFileName (this, MixerWindow::tr ("Load Sound"), window.folder(), audioFilter());
            if (! file.isEmpty())
                window.loadInstrumentSound (index, file);
        })->setEnabled (sampled);
        menu.addAction (MixerWindow::tr ("Take Sound from the Sample &Menu"), this, [this] { window.takeInstrumentSound (index); })->setEnabled (sampled);
        menu.addSeparator();
        const bool hasSound = audio != nullptr || midi != nullptr;
        menu.addAction (MixerWindow::tr ("&Bounce in Place (B)"), this, [this] { window.bounce (index, true); })->setEnabled (hasSound);
        menu.addAction (MixerWindow::tr ("Bounce to &New Track (Shift+B)"), this, [this] { window.bounce (index, false); })
            ->setEnabled (hasSound && window.engine.numTracks() < mixer::maxTracks);
        menu.addSeparator();
        menu.addAction (MixerWindow::tr ("&Colour..."), this, [this]
        {
            const auto c = QColorDialog::getColor (window.info[(size_t) index].colour, this, MixerWindow::tr ("Track Colour"));
            if (c.isValid())
            {
                window.info[(size_t) index].colour = c;
                refresh();
                window.changed();
            }
        });
        menu.exec (e->globalPos());
    }

    void offsetDialog (bool audioClip)
    {
        auto& s = strip();
        bool ok = false;
        const auto value = QInputDialog::getDouble (this, audioClip ? MixerWindow::tr ("Audio Start") : MixerWindow::tr ("MIDI Start"),
                                                    MixerWindow::tr ("Where the file starts, in milliseconds from the start of the mix\n"
                                                                     "(to line up a stem another program exported late, use a negative value):"),
                                                    (audioClip ? s.audioOffset.load() : s.midiOffset.load()) * 1000.0, -600000.0, 3600000.0, 1, &ok);
        if (! ok)
            return;
        (audioClip ? s.audioOffset : s.midiOffset) = value / 1000.0;
        window.changed();
    }

    void dragEnterEvent (QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dropEvent (QDropEvent* e) override
    {
        for (const auto& url : e->mimeData()->urls())
        {
            if (! url.isLocalFile())
                continue;
            const auto file = url.toLocalFile();
            window.select (index);
            if (isMidiFile (file))
            {
                QString error;
                if (! window.loadMidi (index, file, error))
                    QMessageBox::warning (&window, MixerWindow::tr ("MIDI File"), error);
            }
            else
                window.loadAudio (index, file);
        }
        e->acceptProposedAction();
    }

public:
    MixerWindow& window;
    const int index;
    bool updating = false;
    QFrame* colourBar;
    QLabel* number;
    QLineEdit* nameEdit;
    QCheckBox* audioOn;
    QPushButton* audioButton;
    QCheckBox* midiOn;
    QPushButton* midiButton;
    QComboBox* instrumentBox;
    QPushButton* editButton;
    QSpinBox* transposeBox;
    QPushButton* channelsButton;
    QPushButton* armButton;
    QDial* trimDial;
    QLabel* trimLabel;
    QPushButton* invertL;
    QPushButton* invertR;
    QDial* panDial;
    QLabel* panLabel;
    QPushButton* muteButton;
    QPushButton* soloButton;
    QSlider* fader;
    LevelMeter* meter;
    QLabel* faderValue;
    QLabel* peakValue;
};

//==============================================================================
//==============================================================================
// The tracks across time, one horizontal row each: the audio file's waveform and
// the MIDI file's notes (both, stacked, when the track has both), the bars, the
// playhead and the loop. Click to move the playhead and pick the track; Ctrl+wheel
// zooms, the wheel scrolls, a double-click on the ruler fits the whole mix.
class TrackLanes final : public QWidget
{
public:
    explicit TrackLanes (MixerWindow& w) : QWidget (&w), window (w)
    {
        setAcceptDrops (true);
        setFocusPolicy (Qt::NoFocus);
        setMinimumHeight (120);
        setMouseTracking (false);
        setToolTip (MixerWindow::tr ("The tracks across time. Click to move the playhead; Ctrl+wheel zooms, "
                                     "the wheel scrolls, double-click the ruler to see the whole mix. V shows and hides them."));
    }

    QSize sizeHint() const override { return { 900, 260 }; }

    // Called by the window's timer: keeps the playhead in view while zoomed in.
    void follow()
    {
        if (fit || ! window.engine.isPlaying())
            return;
        const auto p = (double) window.engine.position();
        const auto span = framesPerPixel * timelineWidth();
        if (p < start || p > start + span)
            start = std::max (0.0, p - span * 0.05);
    }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        const auto pal = palette();
        g.fillRect (rect(), pal.color (QPalette::Base));
        updateScale();

        const int count = window.engine.numTracks();
        const auto r = window.rate();
        const auto bpm = window.tempoBox->value();
        const auto text = pal.color (QPalette::Text);
        auto faint = text;
        faint.setAlpha (28);
        auto softer = text;
        softer.setAlpha (150);

        // The ruler: bars, labelled so they stay apart.
        g.fillRect (QRect (0, 0, width(), rulerHeight), pal.color (QPalette::Window));
        if (window.engine.isLooping())
        {
            auto loop = Theme::current().color (QStringLiteral ("selection"));
            loop.setAlpha (110);
            g.fillRect (QRectF (xOf (0.0), 0, xOf ((double) window.engine.length()) - xOf (0.0), rulerHeight), loop);
        }
        const auto barFrames = 4.0 * 60.0 / bpm * r;
        int every = 1;
        while (every < 4096 && barFrames * every / framesPerPixel < 56.0)
            every *= 2;
        const auto firstBar = std::max (0, (int) std::floor (start / barFrames));
        const auto lastBar = (int) std::ceil ((start + framesPerPixel * timelineWidth()) / barFrames);
        g.setFont (smallFont());
        for (int bar = firstBar; bar <= lastBar; ++bar)
        {
            const auto x = xOf (bar * barFrames);
            if (x < headerWidth)
                continue;
            const bool labelled = bar % every == 0;
            if (! labelled && barFrames / framesPerPixel < 6.0)
                continue;
            g.setPen (labelled ? faint.darker (100) : faint);
            g.drawLine (QPointF (x, labelled ? 0 : rulerHeight - 6), QPointF (x, height()));
            if (labelled)
            {
                g.setPen (softer);
                g.drawText (QPointF (x + 3, rulerHeight - 6), QString::number (bar + 1));
            }
        }
        g.setPen (faint);
        g.drawLine (0, rulerHeight - 1, width(), rulerHeight - 1);

        // The rows.
        bool anySolo = false;
        for (int i = 0; i < count; ++i)
            anySolo = anySolo || window.engine.strip (i).solo.load();
        const auto rowH = rowHeight();
        for (int i = 0; i < count; ++i)
        {
            const QRect row (0, rulerHeight + i * rowH, width(), rowH);
            const auto& s = window.engine.strip (i);
            const auto colour = window.info[(size_t) i].colour;
            const bool heard = ! s.mute.load() && (! anySolo || s.solo.load());
            if (i == window.selected)
            {
                auto tint = colour;
                tint.setAlpha (34);
                g.fillRect (row, tint);
            }
            g.setPen (faint);
            g.drawLine (row.bottomLeft(), row.bottomRight());

            // The header: colour, number and name, and what is on.
            const QRect header (0, row.top(), headerWidth, rowH);
            g.fillRect (header, pal.color (QPalette::Window));
            g.fillRect (QRect (0, row.top(), 5, rowH - 1), colour);
            g.setPen (text);
            auto bold = smallFont();
            bold.setBold (true);
            g.setFont (bold);
            g.drawText (header.adjusted (10, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                        QStringLiteral ("%1  %2").arg (i + 1).arg (g.fontMetrics().elidedText (window.info[(size_t) i].name, Qt::ElideRight, headerWidth - 34)));
            QStringList flags;
            if (s.mute.load())
                flags << MixerWindow::tr ("M");
            if (s.solo.load())
                flags << MixerWindow::tr ("S");
            if (window.engine.liveTrack() == i)
                flags << QStringLiteral ("●");
            g.setFont (smallFont());
            g.setPen (softer);
            if (! flags.isEmpty() && rowH >= 34)
                g.drawText (header.adjusted (10, 0, -4, -3), Qt::AlignLeft | Qt::AlignBottom, flags.join (QLatin1Char (' ')));

            // The clips, stacked when there are both.
            const auto audioClip = window.engine.audio (i);
            const auto midiClip = window.engine.midi (i);
            const QRect lane (headerWidth, row.top() + 2, timelineWidth(), rowH - 5);
            g.save();
            g.setClipRect (lane);
            if (audioClip != nullptr && midiClip != nullptr)
            {
                const auto split = lane.top() + lane.height() * 3 / 5;
                drawAudio (g, i, *audioClip, QRect (lane.left(), lane.top(), lane.width(), split - lane.top()), s, heard);
                drawMidi (g, i, *midiClip, QRect (lane.left(), split + 1, lane.width(), lane.bottom() - split), s, heard);
            }
            else if (audioClip != nullptr)
                drawAudio (g, i, *audioClip, lane, s, heard);
            else if (midiClip != nullptr)
                drawMidi (g, i, *midiClip, lane, s, heard);
            else if (rowH >= 30)
            {
                g.setPen (faint.darker (100));
                g.setFont (smallFont());
                g.drawText (lane.adjusted (8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                            MixerWindow::tr ("Drop an audio or MIDI file here"));
            }
            g.restore();
        }

        // The playhead.
        const auto px = xOf ((double) window.engine.position());
        if (px >= headerWidth && px <= width())
        {
            g.setPen (QPen (Theme::current().color (QStringLiteral ("highlight")), 1.5));
            g.drawLine (QPointF (px, 0), QPointF (px, rulerHeight + count * rowH));
        }
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        const auto pos = e->position();
        const int row = rowAt (pos.y());
        if (row >= 0)
            window.select (row);
        if (pos.x() >= headerWidth && e->button() == Qt::LeftButton)
        {
            locate (pos.x());
            scrubbing = true;
        }
        window.setFocus();
        update();
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        if (scrubbing && (e->buttons() & Qt::LeftButton))
            locate (e->position().x());
    }

    void mouseReleaseEvent (QMouseEvent*) override { scrubbing = false; }

    void mouseDoubleClickEvent (QMouseEvent* e) override
    {
        if (e->position().y() < rulerHeight)
        {
            fit = true;
            update();
        }
    }

    void wheelEvent (QWheelEvent* e) override
    {
        const auto steps = (double) e->angleDelta().y() / 120.0 + (double) e->angleDelta().x() / 120.0;
        if (std::abs (steps) < 1.0e-9)
            return;
        updateScale();
        if (e->modifiers() & Qt::ControlModifier)
        {
            const auto x = std::max ((double) headerWidth, e->position().x());
            const auto at = start + (x - headerWidth) * framesPerPixel;
            framesPerPixel = std::clamp (framesPerPixel * std::pow (0.8, steps), 4.0, std::max (4.0, fitFramesPerPixel() * 4.0));
            start = std::max (0.0, at - (x - headerWidth) * framesPerPixel);
        }
        else
            start = std::max (0.0, start - steps * 80.0 * framesPerPixel);
        fit = false;
        e->accept();
        update();
    }

    void dragEnterEvent (QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dropEvent (QDropEvent* e) override
    {
        const int row = rowAt (e->position().y());
        if (row < 0)
            return;
        window.select (row);
        for (const auto& url : e->mimeData()->urls())
        {
            if (! url.isLocalFile())
                continue;
            const auto file = url.toLocalFile();
            if (isMidiFile (file))
            {
                QString error;
                if (! window.loadMidi (row, file, error))
                    QMessageBox::warning (&window, MixerWindow::tr ("MIDI File"), error);
            }
            else
                window.loadAudio (row, file);
        }
        e->acceptProposedAction();
        update();
    }

private:
    static constexpr int headerWidth = 132;
    static constexpr int rulerHeight = 20;
    static constexpr Frame peakBlock = 256;   // frames per waveform peak

    // Pictures of the clips, made again when a clip changes.
    struct Picture
    {
        mixer::AudioClipPtr audio;
        std::vector<std::pair<float, float>> peaks;   // min and max of both sides, per block
        mixer::MidiClipPtr midi;
        struct Note { Frame on, off; int pitch, channel; };
        std::vector<Note> notes;
        int low = 60, high = 72;
    };

    QFont smallFont() const
    {
        auto f = font();
        f.setPointSizeF (std::max (7.0, f.pointSizeF() * 0.85));
        return f;
    }

    int timelineWidth() const { return std::max (1, width() - headerWidth); }

    int rowHeight() const
    {
        const auto count = std::max (1, window.engine.numTracks());
        return std::clamp ((height() - rulerHeight) / count, 22, 140);
    }

    int rowAt (double y) const
    {
        if (y < rulerHeight)
            return window.selected < mixer::masterIndex ? window.selected : -1;
        const auto row = (int) ((y - rulerHeight) / rowHeight());
        return row < window.engine.numTracks() ? row : -1;
    }

    double fitFramesPerPixel() const
    {
        const auto r = window.rate();
        const auto span = std::max ((double) window.engine.length() * 1.04, 16.0 * r);
        return span / timelineWidth();
    }

    void updateScale()
    {
        if (fit)
        {
            framesPerPixel = fitFramesPerPixel();
            start = 0.0;
        }
    }

    double xOf (double frame) const { return headerWidth + (frame - start) / framesPerPixel; }

    void locate (double x)
    {
        const auto frame = std::max (0.0, start + (x - headerWidth) * framesPerPixel);
        window.engine.setPosition ((Frame) std::llround (frame));
        window.refreshTransport();
        update();
    }

    const Picture& picture (int track)
    {
        auto& p = pictures[(size_t) track];
        const auto audio = window.engine.audio (track);
        if (audio != p.audio)
        {
            p.audio = audio;
            p.peaks.clear();
            if (audio != nullptr)
            {
                const auto n = audio->length();
                p.peaks.reserve ((size_t) (n / peakBlock + 1));
                for (Frame at = 0; at < n; at += peakBlock)
                {
                    float lo = 0.0f, hi = 0.0f;
                    const auto end = std::min (n, at + peakBlock);
                    for (Frame f = at; f < end; ++f)
                    {
                        const auto l = audio->left[(size_t) f], r = audio->right[(size_t) f];
                        lo = std::min ({ lo, l, r });
                        hi = std::max ({ hi, l, r });
                    }
                    p.peaks.push_back ({ lo, hi });
                }
            }
        }
        const auto midi = window.engine.midi (track);
        if (midi != p.midi)
        {
            p.midi = midi;
            p.notes.clear();
            p.low = 127;
            p.high = 0;
            if (midi != nullptr)
            {
                std::array<std::array<Frame, 128>, 16> on;
                for (auto& c : on)
                    c.fill (-1);
                for (const auto& e : midi->events)
                {
                    const auto type = e.status & 0xF0;
                    const auto channel = e.status & 0x0F;
                    if (type != 0x80 && type != 0x90)
                        continue;
                    auto& held = on[(size_t) channel][e.data1 & 0x7F];
                    if (held >= 0)
                    {
                        p.notes.push_back ({ held, e.frame, e.data1 & 0x7F, channel });
                        held = -1;
                    }
                    if (type == 0x90 && e.data2 > 0)
                        held = e.frame;
                }
                for (int c = 0; c < 16; ++c)
                    for (int n = 0; n < 128; ++n)
                        if (on[(size_t) c][(size_t) n] >= 0)
                            p.notes.push_back ({ on[(size_t) c][(size_t) n], midi->length, n, c });
                for (const auto& n : p.notes)
                {
                    p.low = std::min (p.low, n.pitch);
                    p.high = std::max (p.high, n.pitch);
                }
                if (p.low > p.high)
                    p.low = p.high = 60;
            }
        }
        return p;
    }

    QColor clipColour (int track, bool on, bool heard, int alpha) const
    {
        auto c = window.info[(size_t) track].colour;
        if (! on || ! heard)
            c = QColor::fromHsvF (c.hsvHueF(), c.hsvSaturationF() * 0.25f, c.valueF() * 0.75f);
        c.setAlpha (alpha);
        return c;
    }

    void drawAudio (QPainter& g, int track, const mixer::AudioClip& clip, QRect area, const mixer::Strip& s, bool heard)
    {
        const auto& p = picture (track);
        const auto offset = s.audioOffset.load() * window.rate();
        const auto left = std::max ((double) area.left(), xOf (offset));
        const auto right = std::min ((double) area.right(), xOf (offset + (double) clip.length()));
        if (right <= left)
            return;
        const bool on = s.audioOn.load();
        g.fillRect (QRectF (left, area.top(), right - left, area.height()), clipColour (track, on, heard, 40));
        g.setPen (clipColour (track, on, heard, 230));
        const auto mid = area.top() + area.height() / 2.0;
        const auto half = area.height() / 2.0 - 1.0;
        for (int x = (int) left; x < (int) right; ++x)
        {
            const auto from = (start + (x - headerWidth) * framesPerPixel - offset) / peakBlock;
            const auto to = (start + (x + 1 - headerWidth) * framesPerPixel - offset) / peakBlock;
            auto a = (size_t) std::max (0.0, std::floor (from));
            if (a >= p.peaks.size())
                break;
            const auto b = std::min (p.peaks.size(), std::max (a + 1, (size_t) std::max (0.0, std::ceil (to))));
            float lo = 0.0f, hi = 0.0f;
            for (; a < b; ++a)
            {
                lo = std::min (lo, p.peaks[a].first);
                hi = std::max (hi, p.peaks[a].second);
            }
            g.drawLine (QPointF (x + 0.5, mid - std::min (1.0f, hi) * half), QPointF (x + 0.5, mid - std::max (-1.0f, lo) * half));
        }
    }

    void drawMidi (QPainter& g, int track, const mixer::MidiClip& clip, QRect area, const mixer::Strip& s, bool heard)
    {
        const auto& p = picture (track);
        const auto offset = s.midiOffset.load() * window.rate();
        const auto left = std::max ((double) area.left(), xOf (offset));
        const auto right = std::min ((double) area.right(), xOf (offset + (double) clip.length));
        if (right <= left)
            return;
        const bool on = s.midiOn.load();
        g.fillRect (QRectF (left, area.top(), right - left, area.height()), clipColour (track, on, heard, 28));
        const auto range = std::max (12, p.high - p.low + 1);
        const auto low = p.low - (range - (p.high - p.low + 1)) / 2;
        const auto noteH = std::max (1.0, (double) area.height() / range);
        const auto colour = clipColour (track, on, heard, 235);
        const auto channels = s.channels.load();
        auto muted = colour;
        muted.setAlpha (70);
        for (const auto& n : p.notes)
        {
            const auto x0 = xOf (offset + (double) n.on), x1 = xOf (offset + (double) n.off);
            if (x1 < area.left() || x0 > area.right())
                continue;
            const auto y = area.bottom() - (n.pitch - low + 1) * (double) area.height() / range;
            const bool playing = (channels & (1u << n.channel)) != 0;
            g.fillRect (QRectF (x0, y, std::max (1.5, x1 - x0), noteH), playing ? colour : muted);
        }
    }

    MixerWindow& window;
    std::array<Picture, mixer::maxTracks> pictures;
    bool fit = true;
    bool scrubbing = false;
    double start = 0.0;            // the frame at the left edge of the timeline
    double framesPerPixel = 1000.0;
};

MixerWindow::MixerWindow (mixer::Engine& e, AudioEngine& a, Hooks h, QWidget* parent)
    : QWidget (parent, Qt::Window), engine (e), audio (a), hooks (std::move (h))
{
    setWindowTitle (tr ("Mixer"));
    setObjectName (QStringLiteral ("MixerWindow"));
    setFocusPolicy (Qt::StrongFocus);
    restoring = true;   // nothing below is a change to the mix
    Theme::makeTranslucent (this);   // the same theme and window opacity as the rest of PHINYX
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        info[(size_t) i].name = tr ("Track %1").arg (i + 1);
        info[(size_t) i].colour = trackColours[i];
    }

    // The transport and the mix.
    auto button = [this] (const QString& text, const QString& tip)
    {
        auto* b = new QPushButton (text, this);
        b->setFocusPolicy (Qt::NoFocus);
        b->setToolTip (tip);
        return b;
    };
    auto* toStartButton = button (QStringLiteral ("⏮"), tr ("To the start (Home)"));
    playButton = button (tr ("Play"), tr ("Play or stop the mix (Space)"));
    loopButton = button (tr ("Loop"), tr ("Play the whole mix over and over (L)"));
    loopButton->setCheckable (true);
    positionLabel = new QLabel (this);
    positionLabel->setMinimumWidth (190);
    auto mono = positionLabel->font();
    mono.setFamily (QStringLiteral ("monospace"));
    mono.setStyleHint (QFont::Monospace);
    positionLabel->setFont (mono);
    tempoBox = new QDoubleSpinBox (this);
    tempoBox->setRange (20.0, 400.0);
    tempoBox->setDecimals (2);
    tempoBox->setValue (120.0);
    tempoBox->setSuffix (tr (" BPM"));
    tempoBox->setToolTip (tr ("The mix's tempo, for the bars and beats shown; the first MIDI file loaded sets it"));
    followTempoBox = new QCheckBox (tr ("MIDI follows tempo"), this);
    followTempoBox->setFocusPolicy (Qt::NoFocus);
    followTempoBox->setToolTip (tr ("Play the MIDI files at the mix's tempo instead of their own tempo maps"));
    auto* addButton = button (tr ("+ Track"), tr ("Add a track, up to 8 (Ctrl+T)"));
    auto* removeButton = button (tr ("− Track"), tr ("Remove the last track (Ctrl+Shift+T)"));
    auto* openButton = button (tr ("Open..."), tr ("Open a mix (Ctrl+O)"));
    auto* saveButton = button (tr ("Save"), tr ("Save the mix (Ctrl+S); Ctrl+Shift+S saves it under a new name"));
    auto* mixdownButton = button (tr ("Mix Down to Editor"), tr ("Render the whole mix, with the instruments' tails, into a new tab (Ctrl+Return)"));
    auto* midiButton = button (tr ("MIDI Inputs..."), tr ("Choose the MIDI keyboards that play the armed track"));
    midiStatus = new QLabel (this);

    auto* top = new QHBoxLayout;
    top->addWidget (toStartButton);
    top->addWidget (playButton);
    top->addWidget (loopButton);
    top->addWidget (positionLabel);
    top->addWidget (tempoBox);
    top->addWidget (followTempoBox);
    top->addStretch (1);
    top->addWidget (addButton);
    top->addWidget (removeButton);
    top->addWidget (openButton);
    top->addWidget (saveButton);
    top->addWidget (mixdownButton);

    // The strips, then the master.
    stripArea = new QWidget (this);
    stripRow = new QHBoxLayout (stripArea);
    stripRow->setContentsMargins (0, 0, 0, 0);
    stripRow->setSpacing (4);
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        strips[(size_t) i] = new MixerStrip (*this, i);
        stripRow->addWidget (strips[(size_t) i]);
    }
    stripRow->addStretch (1);
    auto* scroll = new QScrollArea (this);
    scroll->setWidget (stripArea);
    scroll->setWidgetResizable (true);
    scroll->setFrameShape (QFrame::NoFrame);
    scroll->setFocusPolicy (Qt::NoFocus);
    scroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);

    masterStrip = new QFrame (this);
    masterStrip->setObjectName (QStringLiteral ("masterStrip"));
    static_cast<QFrame*> (masterStrip)->setFrameShape (QFrame::StyledPanel);
    masterStrip->setMinimumWidth (96);
    auto* masterLayout = new QVBoxLayout (masterStrip);
    masterLayout->setContentsMargins (5, 5, 5, 5);
    auto* masterTitle = new QLabel (tr ("Master"), masterStrip);
    auto bold = masterTitle->font();
    bold.setBold (true);
    masterTitle->setFont (bold);
    masterTitle->setAlignment (Qt::AlignCenter);
    auto* masterNote = new QLabel (tr ("No plugins: mastering\nis the editor's job"), masterStrip);
    masterNote->setAlignment (Qt::AlignCenter);
    masterNote->setEnabled (false);
    masterFader = new QSlider (Qt::Vertical, masterStrip);
    masterFader->setRange (0, faderSteps);
    masterFader->setFocusPolicy (Qt::NoFocus);
    masterFader->setToolTip (tr ("The master out (9 selects it; Up and Down move it)"));
    masterMeter = new LevelMeter (masterStrip);
    masterValue = new QLabel (masterStrip);
    masterValue->setAlignment (Qt::AlignCenter);
    masterPeak = new QLabel (masterStrip);
    masterPeak->setAlignment (Qt::AlignCenter);
    masterLayout->addWidget (masterTitle);
    masterLayout->addWidget (masterNote);
    auto* masterRow = new QHBoxLayout;
    masterRow->addStretch (1);
    masterRow->addWidget (masterFader);
    masterRow->addWidget (masterMeter);
    masterRow->addStretch (1);
    masterLayout->addLayout (masterRow, 1);
    auto* masterValues = new QHBoxLayout;
    masterValues->addWidget (masterValue);
    masterValues->addWidget (masterPeak);
    masterLayout->addLayout (masterValues);

    auto* mixArea = new QWidget (this);
    auto* mixRow = new QHBoxLayout (mixArea);
    mixRow->setContentsMargins (0, 0, 0, 0);
    mixRow->addWidget (scroll, 1);
    mixRow->addWidget (masterStrip);

    // The tracks across time above the strips, as in a DAW's arrangement.
    lanes = new TrackLanes (*this);
    splitter = new QSplitter (Qt::Vertical, this);
    splitter->setChildrenCollapsible (false);
    splitter->addWidget (lanes);
    splitter->addWidget (mixArea);
    splitter->setStretchFactor (0, 2);
    splitter->setStretchFactor (1, 3);
    splitter->restoreState (appSettings().value (QStringLiteral ("mixer/splitter")).toByteArray());
    lanes->setVisible (appSettings().value (QStringLiteral ("mixer/lanes"), true).toBool());

    status = new QLabel (this);
    status->setWordWrap (true);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget (status, 1);
    bottom->addWidget (midiStatus);
    bottom->addWidget (midiButton);

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (top);
    layout->addWidget (splitter, 1);
    layout->addLayout (bottom);

    connect (toStartButton, &QPushButton::clicked, this, [this] { toStart(); });
    connect (playButton, &QPushButton::clicked, this, [this] { togglePlay(); });
    connect (loopButton, &QPushButton::toggled, this, [this] (bool on)
    {
        if (on)
            engine.setLoop (0, engine.length());
        else
            engine.setLoop (0, 0);
    });
    connect (tempoBox, &QDoubleSpinBox::valueChanged, this, [this]
    {
        if (followTempoBox->isChecked())
            placeAllMidi();
        changed();
    });
    connect (tempoBox, &QDoubleSpinBox::editingFinished, this, [this] { setFocus(); });
    connect (followTempoBox, &QCheckBox::toggled, this, [this] { placeAllMidi(); changed(); });
    connect (addButton, &QPushButton::clicked, this, [this] { addTrack(); });
    connect (removeButton, &QPushButton::clicked, this, [this] { removeTrack(); });
    connect (openButton, &QPushButton::clicked, this, [this] { openMix(); });
    connect (saveButton, &QPushButton::clicked, this, [this] { saveMix (false); });
    connect (mixdownButton, &QPushButton::clicked, this, [this] { Q_EMIT mixdownRequested(); });
    connect (midiButton, &QPushButton::clicked, this, [this] { Q_EMIT midiInputsRequested(); });
    connect (masterFader, &QSlider::valueChanged, this, [this] (int v)
    {
        engine.masterDb = faderDb (v);
        masterValue->setText (dbText (engine.masterDb.load()));
        changed();
    });
    masterFader->setValue (faderPosition (0.0));

    timer.setInterval (33);
    connect (&timer, &QTimer::timeout, this, [this] { tick(); });
    timer.start();
    lastTick = QDateTime::currentMSecsSinceEpoch();

    rebuildStrips();
    select (0);
    restoring = false;
    resize (1180, 860);
    status->setText (tr ("Each track can hold an audio file and a MIDI file together. The MIDI file plays the track's instrument; "
                         "R arms a track for the MIDI keyboard. Right-click a strip for more."));
}

MixerWindow::~MixerWindow() = default;

double MixerWindow::rate() const { return engine.sampleRate(); }

QString MixerWindow::folder() const
{
    return appSettings().value (QStringLiteral ("mixer/folder"), QDir::homePath()).toString();
}

void MixerWindow::setMidiStatus (const QString& text) { midiStatus->setText (text); }

void MixerWindow::rebuildStrips()
{
    const auto count = engine.numTracks();
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        strips[(size_t) i]->setVisible (i < count);
        strips[(size_t) i]->refresh();
    }
    if (selected < mixer::masterIndex && selected >= count)
        select (count - 1);
    refreshTransport();
}

void MixerWindow::select (int index)
{
    if (index < mixer::masterIndex)
        index = std::clamp (index, 0, engine.numTracks() - 1);
    selected = index;
    for (int i = 0; i < mixer::maxTracks; ++i)
        strips[(size_t) i]->setSelected (i == selected);
    if (lanes != nullptr)
        lanes->update();
    masterStrip->setStyleSheet (selected == mixer::masterIndex
                                    ? QStringLiteral ("QFrame#masterStrip { border: 2px solid %1; }").arg (Theme::current().color (QStringLiteral ("highlight")).name())
                                    : QString());
}

void MixerWindow::tick()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto seconds = std::clamp ((double) (now - lastTick) / 1000.0, 0.0, 0.5);
    lastTick = now;
    for (int i = 0; i < engine.numTracks(); ++i)
        strips[(size_t) i]->tick (seconds);
    masterMeter->setLevels (engine.takeMeter (mixer::masterIndex), seconds);
    masterPeak->setText (peakText (masterMeter->highestPeak()));
    refreshTransport();
    if (lanes->isVisible())
    {
        lanes->follow();
        lanes->update();
    }
}

void MixerWindow::refreshTransport()
{
    const auto r = rate();
    const auto seconds = (double) engine.position() / r;
    const auto beats = seconds * tempoBox->value() / 60.0;
    const auto minutes = (int) (seconds / 60.0);
    positionLabel->setText (QStringLiteral ("%1:%2  %3.%4  / %5:%6")
                                .arg (minutes).arg (seconds - minutes * 60.0, 6, 'f', 3, QLatin1Char ('0'))
                                .arg ((int) (beats / 4.0) + 1).arg ((int) std::fmod (beats, 4.0) + 1)
                                .arg ((int) (engine.length() / r / 60.0))
                                .arg (std::fmod ((double) engine.length() / r, 60.0), 4, 'f', 1, QLatin1Char ('0')));
    playButton->setText (engine.isPlaying() ? tr ("Stop") : tr ("Play"));
}

void MixerWindow::togglePlay()
{
    if (engine.isPlaying())
        engine.stop();
    else
    {
        if (loopButton->isChecked())
            engine.setLoop (0, engine.length());
        engine.play();
        Q_EMIT playStarted();
    }
    refreshTransport();
}

void MixerWindow::toStart()
{
    engine.setPosition (0);
    refreshTransport();
}

void MixerWindow::addTrack()
{
    if (engine.numTracks() >= mixer::maxTracks)
    {
        status->setText (tr ("The mixer has 8 tracks at most."));
        return;
    }
    engine.setNumTracks (engine.numTracks() + 1);
    rebuildStrips();
    select (engine.numTracks() - 1);
    changed();
}

void MixerWindow::removeTrack()
{
    const auto last = engine.numTracks() - 1;
    if (last < 1)
        return;
    if ((engine.audio (last) != nullptr || engine.midi (last) != nullptr || engine.instrument (last) != nullptr)
        && QMessageBox::question (this, tr ("Remove Track"), tr ("Remove track %1, “%2”, and what is on it?").arg (last + 1).arg (info[(size_t) last].name))
               != QMessageBox::Yes)
        return;
    engine.setNumTracks (last);
    engine.setAudio (last, nullptr);
    engine.setMidi (last, nullptr);
    engine.setInstrument (last, nullptr);
    auto& s = engine.strip (last);
    s.mute = s.solo = false;
    if (info[(size_t) last].editor != nullptr)
        info[(size_t) last].editor->close();
    rebuildStrips();
    changed();
}

void MixerWindow::loadAudio (int track, const QString& file)
{
    auto& t = info[(size_t) track];
    const auto token = ++t.loading;
    t.busy = true;
    strips[(size_t) track]->refresh();
    status->setText (tr ("Loading %1...").arg (QFileInfo (file).fileName()));
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());
    const auto r = rate();
    QPointer<MixerWindow> self (this);
    (void) QtConcurrent::run ([self, track, token, file, r]
    {
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
        auto loaded = AudioEngine::loadFile (file.toStdString(), markers, tags, error);
        mixer::AudioClipPtr clip;
        if (loaded != nullptr)
        {
            if (std::abs (loaded->sampleRate() - r) > 0.5)
                loaded = dsp::resample (*loaded, r);
            clip = mixer::makeAudioClip (*loaded, file.toStdString());
        }
        QMetaObject::invokeMethod (qApp, [self, track, token, file, clip, error]
        {
            if (self == nullptr || self->info[(size_t) track].loading != token)
                return;
            self->info[(size_t) track].busy = false;
            if (clip == nullptr)
            {
                self->status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (error)));
                self->strips[(size_t) track]->refresh();
                return;
            }
            self->engine.setAudio (track, clip);
            self->engine.strip (track).audioOn = true;
            if (self->loopButton->isChecked())
                self->engine.setLoop (0, self->engine.length());
            self->strips[(size_t) track]->refresh();
            self->status->setText (tr ("%1 is on track %2").arg (QFileInfo (file).fileName()).arg (track + 1));
            if (! self->restoring)
                self->changed();
        }, Qt::QueuedConnection);
    });
}

bool MixerWindow::loadMidi (int track, const QString& file, QString& error)
{
    std::string why;
    auto song = midifile::read (file.toStdString(), why);
    if (song == nullptr)
    {
        error = tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (why));
        return false;
    }
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());
    // The first MIDI file sets the mix's tempo.
    bool first = true;
    for (int i = 0; i < mixer::maxTracks; ++i)
        first = first && engine.midi (i) == nullptr;
    if (first && ! restoring && song->ticksPerQuarter > 0)
    {
        const QSignalBlocker block (tempoBox);
        tempoBox->setValue (song->fileBpm());
    }
    engine.setMidi (track, mixer::placeMidi (song, file.toStdString(), rate(), followTempoBox->isChecked() ? tempoBox->value() : 0.0));
    engine.strip (track).midiOn = true;
    // A MIDI file needs something to play it: the Synth until another instrument is chosen.
    if (engine.instrument (track) == nullptr && ! restoring)
        setInstrumentKind (track, synthChoice, {});
    if (loopButton->isChecked())
        engine.setLoop (0, engine.length());
    strips[(size_t) track]->refresh();
    status->setText (tr ("%1 is on track %2: %3 notes, %4 s").arg (QFileInfo (file).fileName()).arg (track + 1)
                         .arg (song->noteCount()).arg (engine.midi (track)->length / rate(), 0, 'f', 1));
    if (! restoring)
        changed();
    return true;
}

void MixerWindow::chooseAudio (int track)
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("Audio File for Track %1").arg (track + 1), folder(), audioFilter());
    if (! file.isEmpty())
        loadAudio (track, file);
    setFocus();
}

void MixerWindow::chooseMidi (int track)
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("MIDI File for Track %1").arg (track + 1), folder(),
                                                    tr ("MIDI files (*.mid *.midi *.smf *.rmi);;All files (*)"));
    QString error;
    if (! file.isEmpty() && ! loadMidi (track, file, error))
        QMessageBox::warning (this, tr ("MIDI File"), error);
    setFocus();
}

void MixerWindow::placeAllMidi()
{
    const auto bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    for (int i = 0; i < mixer::maxTracks; ++i)
        if (const auto m = engine.midi (i); m != nullptr)
            engine.setMidi (i, mixer::placeMidi (m->song, m->path, rate(), bpm));
    if (loopButton->isChecked())
        engine.setLoop (0, engine.length());
}

void MixerWindow::setInstrumentKind (int track, int choice, const QString& pluginId)
{
    auto& t = info[(size_t) track];
    if (t.editor != nullptr)
        t.editor->close();
    t.sound = nullptr;
    mixer::InstrumentPtr made;
    switch (choice)
    {
        case synthChoice: made = std::make_shared<mixer::Synth>(); break;
        case samplerChoice:
        case slicerChoice:
            if (choice == samplerChoice)
                made = std::make_shared<mixer::SamplerInstrument>();
            else
                made = std::make_shared<mixer::SlicerInstrument>();
            break;
        case firstPlugin:
        {
            std::string error;
            made = audio.loadInstrumentPlugin (pluginId.toStdString(), rate(), error);
            if (made == nullptr)
            {
                QMessageBox::warning (this, tr ("Instrument"), tr ("The instrument could not be started: %1").arg (QString::fromStdString (error)));
                strips[(size_t) track]->refresh();
                return;
            }
            break;
        }
        default: break;
    }
    engine.setInstrument (track, made);
    strips[(size_t) track]->refresh();
    if (! restoring)
        changed();

    // A new Sampler or Slicer takes the Sample menu's sound if it has one, else asks for a file.
    if ((choice == samplerChoice || choice == slicerChoice) && ! restoring)
    {
        const auto fromMenu = choice == samplerChoice ? (hooks.samplerSound ? hooks.samplerSound().first != nullptr : false)
                                                      : (hooks.slicerSound ? hooks.slicerSound().first != nullptr : false);
        if (fromMenu)
            takeInstrumentSound (track);
        else
        {
            const auto file = QFileDialog::getOpenFileName (this, tr ("Sound for Track %1's %2").arg (track + 1).arg (choice == samplerChoice ? tr ("Sampler") : tr ("Slicer")),
                                                            folder(), audioFilter());
            if (! file.isEmpty())
                loadInstrumentSound (track, file);
        }
    }
    if (choice == firstPlugin && made != nullptr && made->hasWindow() && ! restoring)
        made->showWindow();
}

void MixerWindow::loadInstrumentSound (int track, const QString& file)
{
    const auto inst = engine.instrument (track);
    if (inst == nullptr || (inst->kind() != mixer::Instrument::Kind::sampler && inst->kind() != mixer::Instrument::Kind::slicer))
        return;
    std::vector<Marker> markers;
    Tags tags;
    std::string error;
    auto loaded = AudioEngine::loadFile (file.toStdString(), markers, tags, error);
    if (loaded == nullptr)
    {
        QMessageBox::warning (this, tr ("Load Sound"), tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (error)));
        return;
    }
    if (std::abs (loaded->sampleRate() - rate()) > 0.5)
        loaded = dsp::resample (*loaded, rate());
    auto sound = sampler::makeSample (*loaded);
    info[(size_t) track].sound = loaded;
    if (inst->kind() == mixer::Instrument::Kind::sampler)
    {
        auto& s = static_cast<mixer::SamplerInstrument&> (*inst);
        s.engine().setSample (sound);
        auto settings = s.engine().settings();
        settings.start = settings.loopStart = 0;
        settings.end = settings.loopEnd = sound->length();
        s.engine().setSettings (settings);
        s.samplePath = file.toStdString();
    }
    else
    {
        auto& s = static_cast<mixer::SlicerInstrument&> (*inst);
        s.engine().setSample (sound);
        auto kit = s.engine().kit();
        slicer::assign (kit, slicer::equalSlices (sound->length(), 16));
        s.engine().setKit (kit);
        s.samplePath = file.toStdString();
    }
    status->setText (tr ("Track %1's %2 plays %3").arg (track + 1).arg (QString::fromStdString (inst->name()), QFileInfo (file).fileName()));
    if (! restoring)
        changed();
}

void MixerWindow::takeInstrumentSound (int track)
{
    const auto inst = engine.instrument (track);
    if (inst == nullptr)
        return;
    if (inst->kind() == mixer::Instrument::Kind::sampler && hooks.samplerSound)
    {
        auto [sound, settings] = hooks.samplerSound();
        if (sound == nullptr)
        {
            status->setText (tr ("The Sampler has no sound yet: send one with F7 first."));
            return;
        }
        auto& s = static_cast<mixer::SamplerInstrument&> (*inst);
        s.engine().setSample (sound);
        s.engine().setSettings (settings);
        s.samplePath.clear();   // saved next to the mix
        info[(size_t) track].sound = soundAsAudio (*sound, rate());
    }
    else if (inst->kind() == mixer::Instrument::Kind::slicer && hooks.slicerSound)
    {
        auto [sound, kit] = hooks.slicerSound();
        if (sound == nullptr)
        {
            status->setText (tr ("The Slicer has no sound yet: send one with F8 first."));
            return;
        }
        auto& s = static_cast<mixer::SlicerInstrument&> (*inst);
        s.engine().setSample (sound);
        s.engine().setKit (kit);
        s.samplePath.clear();
        info[(size_t) track].sound = soundAsAudio (*sound, rate());
    }
    else
        return;
    status->setText (tr ("Track %1's %2 has the Sample menu's sound").arg (track + 1).arg (QString::fromStdString (inst->name())));
    changed();
}

void MixerWindow::editInstrument (int track)
{
    const auto inst = engine.instrument (track);
    if (inst == nullptr)
        return;
    auto& t = info[(size_t) track];
    if (t.editor != nullptr)
    {
        t.editor->show();
        t.editor->raise();
        t.editor->activateWindow();
        return;
    }
    const auto title = tr ("Track %1: %2").arg (track + 1).arg (t.name);
    switch (inst->kind())
    {
        case mixer::Instrument::Kind::plugin:
            inst->showWindow();
            return;
        case mixer::Instrument::Kind::sampler:
        {
            auto& s = static_cast<mixer::SamplerInstrument&> (*inst);
            auto* w = new SamplerWindow (s.engine(), this);
            w->setAttribute (Qt::WA_DeleteOnClose);
            if (const auto sound = s.engine().sample(); sound != nullptr)
                w->setSample (sound, rate(), title, true);
            w->setWindowTitle (tr ("Sampler: %1").arg (title));
            t.editor = w;
            break;
        }
        case mixer::Instrument::Kind::slicer:
        {
            auto& s = static_cast<mixer::SlicerInstrument&> (*inst);
            auto* w = new SlicerWindow (s.engine(), this);
            w->setAttribute (Qt::WA_DeleteOnClose);
            if (const auto sound = s.engine().sample(); sound != nullptr)
                w->setSample (sound, t.sound != nullptr ? t.sound : soundAsAudio (*sound, rate()), rate(), title, true, BeatGrid {}, {});
            w->setWindowTitle (tr ("Slicer: %1").arg (title));
            t.editor = w;
            break;
        }
        case mixer::Instrument::Kind::synth:
        {
            auto& synth = static_cast<mixer::Synth&> (*inst);
            auto* d = new QDialog (this);
            d->setAttribute (Qt::WA_DeleteOnClose);
            d->setWindowTitle (tr ("Synth: %1").arg (title));
            auto* form = new QFormLayout (d);
            const auto s = synth.settings();
            auto* wave = new QComboBox (d);
            wave->addItems ({ tr ("Sine"), tr ("Triangle"), tr ("Saw"), tr ("Square") });
            wave->setCurrentIndex (s.wave);
            auto seconds = [d] (double v, double max)
            {
                auto* b = new QDoubleSpinBox (d);
                b->setRange (0.0, max);
                b->setDecimals (3);
                b->setSingleStep (0.01);
                b->setSuffix (tr (" s"));
                b->setValue (v);
                return b;
            };
            auto* attack = seconds (s.attack, 10.0);
            auto* decay = seconds (s.decay, 10.0);
            auto* sustain = new QSpinBox (d);
            sustain->setRange (0, 100);
            sustain->setSuffix (tr (" %"));
            sustain->setValue ((int) std::lround (s.sustain * 100.0));
            auto* release = seconds (s.release, 20.0);
            auto* cutoff = new QSpinBox (d);
            cutoff->setRange (40, 20000);
            cutoff->setSingleStep (100);
            cutoff->setSuffix (tr (" Hz"));
            cutoff->setValue ((int) s.cutoff);
            auto* gain = new QDoubleSpinBox (d);
            gain->setRange (-48.0, 12.0);
            gain->setSuffix (tr (" dB"));
            gain->setValue (s.gainDb);
            auto* voices = new QSpinBox (d);
            voices->setRange (1, mixer::Synth::maxVoices);
            voices->setValue (s.voices);
            auto* drums = new QCheckBox (tr ("Channel 10 plays General MIDI drums"), d);
            drums->setChecked (s.drums);
            form->addRow (tr ("&Wave"), wave);
            form->addRow (tr ("&Attack"), attack);
            form->addRow (tr ("&Decay"), decay);
            form->addRow (tr ("&Sustain"), sustain);
            form->addRow (tr ("&Release"), release);
            form->addRow (tr ("&Low-pass"), cutoff);
            form->addRow (tr ("&Gain"), gain);
            form->addRow (tr ("&Voices"), voices);
            form->addRow (drums);
            auto apply = [=, &synth, this]
            {
                mixer::SynthSettings n;
                n.wave = wave->currentIndex();
                n.attack = attack->value();
                n.decay = decay->value();
                n.sustain = sustain->value() / 100.0;
                n.release = release->value();
                n.cutoff = cutoff->value();
                n.gainDb = gain->value();
                n.voices = voices->value();
                n.drums = drums->isChecked();
                synth.setSettings (n);
                changed();
            };
            // The dialog keeps the instrument alive while it is open.
            auto keep = std::make_shared<mixer::InstrumentPtr> (inst);
            connect (d, &QObject::destroyed, this, [keep] { keep->reset(); });
            connect (wave, &QComboBox::currentIndexChanged, d, apply);
            for (auto* b : { attack, decay, release, gain })
                connect (b, &QDoubleSpinBox::valueChanged, d, apply);
            for (auto* b : { sustain, cutoff, voices })
                connect (b, &QSpinBox::valueChanged, d, apply);
            connect (drums, &QCheckBox::toggled, d, apply);
            t.editor = d;
            break;
        }
    }
    if (t.editor != nullptr)
    {
        t.editor->show();
        t.editor->raise();
        t.editor->activateWindow();
    }
}

void MixerWindow::arm (int track)
{
    engine.setLiveTrack (engine.liveTrack() == track ? -1 : track);
    for (int i = 0; i < mixer::maxTracks; ++i)
        strips[(size_t) i]->armButton->setChecked (engine.liveTrack() == i);
    const auto inst = engine.liveTrack() >= 0 ? engine.instrument (engine.liveTrack()) : nullptr;
    status->setText (engine.liveTrack() < 0 ? tr ("No track gets the MIDI keyboard.")
                     : inst == nullptr ? tr ("Track %1 gets the MIDI keyboard, but has no instrument yet.").arg (track + 1)
                                       : tr ("The MIDI keyboard plays track %1's %2.").arg (track + 1).arg (QString::fromStdString (inst->name())));
}

void MixerWindow::changed()
{
    if (restoring)
        return;
    modified = true;
    updateTitle();
}

QString MixerWindow::mixName() const
{
    return path.isEmpty() ? tr ("Untitled mix") : QFileInfo (path).completeBaseName();
}

void MixerWindow::updateTitle()
{
    setWindowTitle (tr ("Mixer: %1%2").arg (mixName(), modified ? QStringLiteral (" *") : QString()));
}

//==============================================================================
void MixerWindow::keyPressEvent (QKeyEvent* e)
{
    const auto key = e->key();
    const auto mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    const bool shift = mods == Qt::ShiftModifier;
    const bool plain = mods == Qt::NoModifier || shift;
    const bool master = selected == mixer::masterIndex;

    auto nudgeFader = [&] (double step)
    {
        if (master)
        {
            masterFader->setValue (faderPosition (std::max (-96.0, (double) engine.masterDb.load()) + step));
            return;
        }
        auto& s = engine.strip (selected);
        const auto from = s.faderDb.load() <= -96.0f ? -60.0 : (double) s.faderDb.load();
        s.faderDb = (float) std::clamp (from + step, -96.0, 6.0);
        strips[(size_t) selected]->refresh();
        changed();
    };
    auto nudgePan = [&] (float step)
    {
        if (master)
            return;
        auto& s = engine.strip (selected);
        s.pan = std::clamp (s.pan.load() + step, -1.0f, 1.0f);
        strips[(size_t) selected]->refresh();
        changed();
    };

    if (mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        const bool withShift = (mods & Qt::ShiftModifier) != 0;
        switch (key)
        {
            case Qt::Key_T: withShift ? removeTrack() : addTrack(); return;
            case Qt::Key_O: openMix(); return;
            case Qt::Key_S: saveMix (withShift); return;
            case Qt::Key_N: newMix(); return;
            case Qt::Key_W: close(); return;
            case Qt::Key_Return:
            case Qt::Key_Enter: Q_EMIT mixdownRequested(); return;
            default: break;
        }
    }
    if (plain)
    {
        if (key >= Qt::Key_1 && key <= Qt::Key_8 && ! shift)
        {
            if (key - Qt::Key_1 < engine.numTracks())
                select (key - Qt::Key_1);
            return;
        }
        switch (key)
        {
            case Qt::Key_9: select (mixer::masterIndex); return;
            case Qt::Key_Left: select (master ? engine.numTracks() - 1 : std::max (0, selected - 1)); return;
            case Qt::Key_Right: select (master || selected + 1 >= engine.numTracks() ? mixer::masterIndex : selected + 1); return;
            case Qt::Key_Up: nudgeFader (shift ? 0.1 : 1.0); return;
            case Qt::Key_Down: nudgeFader (shift ? -0.1 : -1.0); return;
            case Qt::Key_BracketLeft: nudgePan (shift ? -0.01f : -0.05f); return;
            case Qt::Key_BracketRight: nudgePan (shift ? 0.01f : 0.05f); return;
            case Qt::Key_0:
                if (master)
                    masterFader->setValue (faderPosition (0.0));
                else
                {
                    engine.strip (selected).faderDb = 0.0f;
                    strips[(size_t) selected]->refresh();
                    changed();
                }
                return;
            case Qt::Key_Space: togglePlay(); return;
            case Qt::Key_Home: toStart(); return;
            case Qt::Key_L: loopButton->toggle(); return;
            case Qt::Key_V:
                if (! shift)
                {
                    toggleLanes();
                    return;
                }
                break;
            case Qt::Key_B:
                if (shift && ! master)
                {
                    bounce (selected, false);
                    return;
                }
                break;
            default: break;
        }
        if (! master && ! shift)
        {
            auto* strip = strips[(size_t) selected];
            switch (key)
            {
                case Qt::Key_M: strip->muteButton->toggle(); return;
                case Qt::Key_S: strip->soloButton->toggle(); return;
                case Qt::Key_R: arm (selected); return;
                case Qt::Key_A: chooseAudio (selected); return;
                case Qt::Key_I: chooseMidi (selected); return;
                case Qt::Key_E: editInstrument (selected); return;
                case Qt::Key_B: bounce (selected, true); return;
                default: break;
            }
        }
    }
    QWidget::keyPressEvent (e);
}

void MixerWindow::changeEvent (QEvent* e)
{
    if (e->type() == QEvent::ActivationChange && isActiveWindow())
        Q_EMIT activated();
    QWidget::changeEvent (e);
}

void MixerWindow::closeEvent (QCloseEvent* e)
{
    engine.stop();
    for (auto& t : info)
        if (t.editor != nullptr)
            t.editor->close();
    autosave();
    appSettings().setValue (QStringLiteral ("mixer/splitter"), splitter->saveState());
    QWidget::closeEvent (e);
    Q_EMIT closed();
}

//==============================================================================
void MixerWindow::newMix()
{
    if (modified && ! path.isEmpty()
        && QMessageBox::question (this, tr ("New Mix"), tr ("Start a new mix? Changes to %1 that were not saved are lost.").arg (mixName())) != QMessageBox::Yes)
        return;
    engine.stop();
    engine.setPosition (0);
    restoring = true;
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        engine.setAudio (i, nullptr);
        engine.setMidi (i, nullptr);
        engine.setInstrument (i, nullptr);
        auto& s = engine.strip (i);
        s.trimDb = 0.0f;
        s.faderDb = 0.0f;
        s.pan = 0.0f;
        s.invertLeft = s.invertRight = false;
        s.mute = s.solo = false;
        s.audioOn = s.midiOn = true;
        s.audioOffset = s.midiOffset = 0.0;
        s.transpose = 0;
        s.channels = 0xFFFF;
        auto& t = info[(size_t) i];
        if (t.editor != nullptr)
            t.editor->close();
        t.name = tr ("Track %1").arg (i + 1);
        t.colour = trackColours[i];
        t.sound = nullptr;
        ++t.loading;
        t.busy = false;
    }
    engine.setNumTracks (2);
    engine.setLiveTrack (0);
    masterFader->setValue (faderPosition (0.0));
    restoring = false;
    path.clear();
    modified = false;
    rebuildStrips();
    select (0);
    updateTitle();
}

void MixerWindow::openMix()
{
    if (modified && QMessageBox::question (this, tr ("Open Mix"), tr ("Open another mix? Changes to %1 that were not saved are lost.").arg (mixName())) != QMessageBox::Yes)
        return;
    const auto file = QFileDialog::getOpenFileName (this, tr ("Open Mix"), folder(), tr ("PHINYX mixes (*.phinyxmix);;All files (*)"));
    if (file.isEmpty())
        return;
    QString error;
    if (! loadMix (file, error))
        QMessageBox::warning (this, tr ("Open Mix"), error);
    setFocus();
}

bool MixerWindow::saveMix (bool saveAs)
{
    auto file = path;
    if (saveAs || file.isEmpty())
    {
        file = QFileDialog::getSaveFileName (this, tr ("Save Mix"), QDir (folder()).filePath (mixName() + QStringLiteral (".phinyxmix")),
                                             tr ("PHINYX mixes (*.phinyxmix)"));
        if (file.isEmpty())
            return false;
        if (! file.endsWith (QStringLiteral (".phinyxmix"), Qt::CaseInsensitive))
            file += QStringLiteral (".phinyxmix");
    }
    QString error;
    if (! writeMix (file, error))
    {
        QMessageBox::warning (this, tr ("Save Mix"), error);
        return false;
    }
    path = file;
    modified = false;
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());
    updateTitle();
    status->setText (tr ("Saved the mix to %1").arg (file));
    return true;
}

bool MixerWindow::writeMix (const QString& file, QString& error)
{
    const QDir base = QFileInfo (file).absoluteDir();
    QJsonArray tracks;
    for (int i = 0; i < engine.numTracks(); ++i)
    {
        const auto& s = engine.strip (i);
        auto& t = info[(size_t) i];
        QJsonObject o {
            { "name", t.name }, { "colour", t.colour.name() },
            { "audioOn", s.audioOn.load() }, { "midiOn", s.midiOn.load() },
            { "audioStart", s.audioOffset.load() }, { "midiStart", s.midiOffset.load() },
            { "transpose", s.transpose.load() }, { "channels", (int) s.channels.load() },
            { "trim", s.trimDb.load() }, { "fader", s.faderDb.load() }, { "pan", s.pan.load() },
            { "invertLeft", s.invertLeft.load() }, { "invertRight", s.invertRight.load() },
            { "mute", s.mute.load() }, { "solo", s.solo.load() },
        };
        if (const auto a = engine.audio (i))
            o.insert ("audio", storedPath (QString::fromStdString (a->path), base));
        if (const auto m = engine.midi (i))
            o.insert ("midi", storedPath (QString::fromStdString (m->path), base));

        if (const auto inst = engine.instrument (i))
        {
            QJsonObject io;
            // A Sampler or Slicer sound that came from the Sample menu is saved as a WAV next to the mix.
            auto soundFile = [&] (sampler::SamplePtr sound, std::string& samplePath)
            {
                if (sound == nullptr)
                    return QString();
                if (samplePath.empty())
                {
                    const auto folderName = QFileInfo (file).completeBaseName() + tr (" Samples");
                    base.mkpath (folderName);
                    const auto wav = base.filePath (folderName + QStringLiteral ("/Track %1.wav").arg (i + 1));
                    SaveOptions options;
                    options.bitDepth = 32;
                    std::string why;
                    if (AudioEngine::saveFile (wav.toStdString(), *soundAsAudio (*sound, rate()), {}, {}, options, why))
                        samplePath = wav.toStdString();
                    else
                        error = tr ("Track %1's sound could not be saved: %2").arg (i + 1).arg (QString::fromStdString (why));
                }
                return storedPath (QString::fromStdString (samplePath), base);
            };
            switch (inst->kind())
            {
                case mixer::Instrument::Kind::synth:
                    io = { { "kind", "synth" }, { "state", QString::fromStdString (inst->state()) } };
                    break;
                case mixer::Instrument::Kind::sampler:
                {
                    auto& sm = static_cast<mixer::SamplerInstrument&> (*inst);
                    io = { { "kind", "sampler" }, { "sound", soundFile (sm.engine().sample(), sm.samplePath) },
                           { "settings", samplerJson (sm.engine().settings()) } };
                    break;
                }
                case mixer::Instrument::Kind::slicer:
                {
                    auto& sl = static_cast<mixer::SlicerInstrument&> (*inst);
                    io = { { "kind", "slicer" }, { "sound", soundFile (sl.engine().sample(), sl.samplePath) }, { "kit", kitJson (sl.engine().kit()) } };
                    break;
                }
                case mixer::Instrument::Kind::plugin:
                    io = { { "kind", "plugin" }, { "id", QString::fromStdString (AudioEngine::instrumentPluginId (*inst)) },
                           { "name", QString::fromStdString (inst->name()) }, { "state", QString::fromStdString (inst->state()) } };
                    break;
            }
            o.insert ("instrument", io);
        }
        tracks.append (o);
    }
    const QJsonObject root {
        { "phinyxMix", 1 }, { "tempo", tempoBox->value() }, { "midiFollowsTempo", followTempoBox->isChecked() },
        { "master", engine.masterDb.load() }, { "armed", engine.liveTrack() }, { "tracks", tracks },
    };
    QSaveFile out (file);
    if (! out.open (QIODevice::WriteOnly))
    {
        error = tr ("%1 could not be written: %2").arg (file, out.errorString());
        return false;
    }
    out.write (QJsonDocument (root).toJson());
    if (! out.commit())
    {
        error = tr ("%1 could not be written: %2").arg (file, out.errorString());
        return false;
    }
    return error.isEmpty();
}

bool MixerWindow::loadMix (const QString& file, QString& error)
{
    QFile in (file);
    if (! in.open (QIODevice::ReadOnly))
    {
        error = tr ("%1 could not be opened: %2").arg (file, in.errorString());
        return false;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson (in.readAll(), &parse);
    const auto root = doc.object();
    if (doc.isNull() || ! root.contains (QStringLiteral ("phinyxMix")))
    {
        error = tr ("%1 is not a PHINYX mix.").arg (QFileInfo (file).fileName());
        return false;
    }
    const auto keepPath = path;
    path.clear();
    modified = false;
    newMix();
    restoring = true;
    const QDir base = QFileInfo (file).absoluteDir();
    QStringList problems;
    {
        const QSignalBlocker a (tempoBox), b (followTempoBox);
        tempoBox->setValue (root.value ("tempo").toDouble (120.0));
        followTempoBox->setChecked (root.value ("midiFollowsTempo").toBool());
    }
    const auto tracks = root.value ("tracks").toArray();
    engine.setNumTracks (std::clamp ((int) tracks.size(), 1, mixer::maxTracks));
    for (int i = 0; i < std::min ((int) tracks.size(), mixer::maxTracks); ++i)
    {
        const auto o = tracks[i].toObject();
        auto& s = engine.strip (i);
        auto& t = info[(size_t) i];
        t.name = o.value ("name").toString (t.name);
        if (const QColor c (o.value ("colour").toString()); c.isValid())
            t.colour = c;
        s.audioOffset = o.value ("audioStart").toDouble();
        s.midiOffset = o.value ("midiStart").toDouble();
        s.transpose = o.value ("transpose").toInt();
        s.channels = (uint32_t) o.value ("channels").toInt (0xFFFF);
        s.trimDb = (float) o.value ("trim").toDouble();
        s.faderDb = (float) o.value ("fader").toDouble();
        s.pan = (float) o.value ("pan").toDouble();
        s.invertLeft = o.value ("invertLeft").toBool();
        s.invertRight = o.value ("invertRight").toBool();
        s.mute = o.value ("mute").toBool();
        s.solo = o.value ("solo").toBool();

        const auto io = o.value ("instrument").toObject();
        const auto kind = io.value ("kind").toString();
        if (kind == QLatin1String ("synth"))
        {
            setInstrumentKind (i, synthChoice, {});
            if (auto inst = engine.instrument (i))
                inst->setState (io.value ("state").toString().toStdString());
        }
        else if (kind == QLatin1String ("sampler") || kind == QLatin1String ("slicer"))
        {
            setInstrumentKind (i, kind == QLatin1String ("sampler") ? samplerChoice : slicerChoice, {});
            const auto sound = resolvedPath (io.value ("sound").toString(), base);
            if (! sound.isEmpty())
            {
                if (QFileInfo::exists (sound))
                    loadInstrumentSound (i, sound);
                else
                    problems << tr ("Track %1: the sound %2 is missing").arg (i + 1).arg (sound);
            }
            if (auto inst = engine.instrument (i); inst != nullptr && inst->kind() == mixer::Instrument::Kind::sampler)
            {
                auto& sm = static_cast<mixer::SamplerInstrument&> (*inst);
                sm.engine().setSettings (samplerFromJson (io.value ("settings").toObject()));
            }
            else if (inst != nullptr && inst->kind() == mixer::Instrument::Kind::slicer)
            {
                auto& sl = static_cast<mixer::SlicerInstrument&> (*inst);
                sl.engine().setKit (kitFromJson (io.value ("kit").toObject()));
            }
        }
        else if (kind == QLatin1String ("plugin"))
        {
            std::string why;
            auto inst = audio.loadInstrumentPlugin (io.value ("id").toString().toStdString(), rate(), why);
            if (inst != nullptr)
            {
                inst->setState (io.value ("state").toString().toStdString());
                engine.setInstrument (i, inst);
            }
            else
                problems << tr ("Track %1: %2 could not be started (%3)").arg (i + 1).arg (io.value ("name").toString(), QString::fromStdString (why));
        }

        const auto midiFile = resolvedPath (o.value ("midi").toString(), base);
        if (! midiFile.isEmpty())
        {
            QString why;
            if (! loadMidi (i, midiFile, why))
                problems << why;
        }
        const auto audioFile = resolvedPath (o.value ("audio").toString(), base);
        if (! audioFile.isEmpty())
        {
            if (QFileInfo::exists (audioFile))
                loadAudio (i, audioFile);
            else
                problems << tr ("Track %1: %2 is missing").arg (i + 1).arg (audioFile);
        }
        s.audioOn = o.value ("audioOn").toBool (true);
        s.midiOn = o.value ("midiOn").toBool (true);
    }
    engine.masterDb = (float) root.value ("master").toDouble();
    masterFader->setValue (faderPosition (engine.masterDb.load()));
    engine.setLiveTrack (root.value ("armed").toInt (0));
    restoring = false;
    path = file;
    modified = false;
    (void) keepPath;
    rebuildStrips();
    select (0);
    updateTitle();
    appSettings().setValue (QStringLiteral ("mixer/folder"), base.absolutePath());
    status->setText (problems.isEmpty() ? tr ("Opened %1").arg (QFileInfo (file).fileName())
                                        : tr ("Opened %1, with problems: %2").arg (QFileInfo (file).fileName(), problems.join (QStringLiteral ("; "))));
    return true;
}

void MixerWindow::autosave()
{
    const auto file = autosavePath();
    QDir().mkpath (QFileInfo (file).absolutePath());
    QString error;
    writeMix (file, error);
    appSettings().setValue (QStringLiteral ("mixer/lastPath"), path);
}

void MixerWindow::restoreAutosave()
{
    const auto file = autosavePath();
    if (! QFileInfo::exists (file))
        return;
    QString error;
    const auto lastPath = appSettings().value (QStringLiteral ("mixer/lastPath")).toString();
    if (loadMix (file, error))
    {
        // It is the mix that was open, not a new one called "last".
        path = lastPath;
        updateTitle();
    }
}

//==============================================================================
// Bounce: the track's sound (its MIDI through its instrument, and its audio file), before
// its fader and pan, from the start of the mix, written to a WAV and put back as audio.
void MixerWindow::bounce (int track, bool inPlace)
{
    if (track < 0 || track >= engine.numTracks())
        return;
    if (engine.audio (track) == nullptr && engine.midi (track) == nullptr)
    {
        status->setText (tr ("Track %1 has nothing on it to bounce.").arg (track + 1));
        return;
    }
    if (! inPlace && engine.numTracks() >= mixer::maxTracks)
    {
        QMessageBox::information (this, tr ("Bounce to New Track"),
                                  tr ("All 8 tracks are in use. Use Bounce in Place (B), or remove a track first."));
        return;
    }
    if (info[(size_t) track].busy)
    {
        status->setText (tr ("Track %1's audio file is still loading.").arg (track + 1));
        return;
    }
    engine.stop();

    // Where it goes: beside the mix, or in the Music folder until the mix is saved.
    const auto base = path.isEmpty() ? QDir (QStandardPaths::writableLocation (QStandardPaths::MusicLocation)).filePath (tr ("PHINYX Bounces"))
                                     : QFileInfo (path).absoluteDir().filePath (QFileInfo (path).completeBaseName() + tr (" Bounces"));
    QDir().mkpath (base);
    auto stem = QStringLiteral ("Track %1 %2").arg (track + 1).arg (info[(size_t) track].name).trimmed();
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    auto file = QDir (base).filePath (stem + QStringLiteral (".wav"));
    for (int n = 2; QFileInfo::exists (file); ++n)
        file = QDir (base).filePath (QStringLiteral ("%1 %2.wav").arg (stem).arg (n));

    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    QProgressDialog dialog (tr ("Bouncing track %1...").arg (track + 1), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (inPlace ? tr ("Bounce in Place") : tr ("Bounce to New Track"));
    dialog.setWindowModality (Qt::WindowModal);
    dialog.setMinimumDuration (300);
    connect (&dialog, &QProgressDialog::canceled, this, [cancel] { cancel->store (true); });

    struct Result { AudioDataPtr data; std::string error; };
    QFutureWatcher<Result> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<Result>::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    connect (&poll, &QTimer::timeout, this, [&] { dialog.setValue ((int) (progress->load() * 1000.0f)); });
    poll.start (50);
    watcher.setFuture (QtConcurrent::run ([m = &engine, track, file, progress, cancel]
    {
        Result r;
        r.data = m->mixdown (10.0, r.error, progress.get(), cancel.get(), track);
        if (r.data != nullptr)
        {
            SaveOptions options;
            options.bitDepth = 32;
            if (! AudioEngine::saveFile (file.toStdString(), *r.data, {}, {}, options, r.error))
                r.data = nullptr;
        }
        return r;
    }));
    loop.exec();
    poll.stop();
    dialog.reset();

    const auto result = watcher.result();
    if (result.data == nullptr)
    {
        if (result.error != "Cancelled.")
            QMessageBox::warning (this, dialog.windowTitle(), result.error.empty() ? tr ("The track could not be bounced.") : QString::fromStdString (result.error));
        return;
    }

    const auto clip = mixer::makeAudioClip (*result.data, file.toStdString());
    auto& from = engine.strip (track);
    int target = track;
    if (inPlace)
    {
        // The MIDI file and the instrument stay, switched off, so turning MIDI back on undoes it;
        // trim and polarity are in the sound now.
        from.midiOn = false;
        from.trimDb = 0.0f;
        from.invertLeft = from.invertRight = false;
    }
    else
    {
        target = engine.numTracks();
        engine.setNumTracks (target + 1);
        auto& to = engine.strip (target);
        to.trimDb = 0.0f;
        to.invertLeft = to.invertRight = false;
        to.faderDb = from.faderDb.load();
        to.pan = from.pan.load();
        to.mute = false;
        to.solo = false;
        to.midiOn = true;
        to.transpose = 0;
        to.channels = 0xFFFF;
        to.midiOffset = 0.0;
        engine.setMidi (target, nullptr);
        engine.setInstrument (target, nullptr);
        info[(size_t) target].name = tr ("%1 bounce").arg (info[(size_t) track].name);
        info[(size_t) target].colour = info[(size_t) track].colour;
        from.mute = true;   // heard once, from the new track
    }
    auto& to = engine.strip (target);
    to.audioOffset = 0.0;   // the bounce starts at the start of the mix
    to.audioOn = true;
    engine.setAudio (target, clip);
    rebuildStrips();
    select (target);
    changed();
    status->setText (inPlace ? tr ("Bounced track %1 in place to %2 (its MIDI file is kept, switched off)").arg (track + 1).arg (file)
                             : tr ("Bounced track %1 to track %2 (%3); track %1 is muted").arg (track + 1).arg (target + 1).arg (file));
}

void MixerWindow::toggleLanes()
{
    lanes->setVisible (! lanes->isVisible());
    appSettings().setValue (QStringLiteral ("mixer/lanes"), lanes->isVisible());
    status->setText (lanes->isVisible() ? tr ("The tracks across time are shown above the strips (V hides them).")
                                        : tr ("The tracks across time are hidden (V shows them)."));
}
