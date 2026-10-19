// SPDX-License-Identifier: AGPL-3.0-only
// Effect > Generate > Tone and Noise.
#include "MainWindow.h"
#include "Settings.h"
#include "../core/Generate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QStatusBar>
#include <QVBoxLayout>

#include <random>

namespace
{
    QDoubleSpinBox* spin (QWidget* parent, double low, double high, int decimals, const QString& suffix, double value)
    {
        auto* box = new QDoubleSpinBox (parent);
        box->setRange (low, high);
        box->setDecimals (decimals);
        box->setSuffix (suffix);
        box->setValue (value);
        box->setAccelerated (true);
        return box;
    }
}

void MainWindow::generateSignal (bool makeNoise)
{
    const auto d = data();
    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("generate"));

    QDialog dialog (this);
    dialog.setWindowTitle (makeNoise ? tr ("Generate Noise") : tr ("Generate Tone"));
    auto* form = new QFormLayout;

    QComboBox* kind = new QComboBox (&dialog);
    if (makeNoise)
    {
        kind->addItems ({ tr ("White"), tr ("Pink"), tr ("Brown (red)"), tr ("Blue"), tr ("Violet") });
        kind->setToolTip (tr ("White: equal energy per hertz, bright hiss. Pink: equal per octave, sounds even (used to tune rooms and mixes). "
                              "Brown: deep rumble, -6 dB per octave. Blue and violet: rising, +3 and +6 dB per octave."));
        kind->setCurrentIndex (settings.value (QStringLiteral ("noise"), 1).toInt());
        form->addRow (tr ("&Colour:"), kind);
    }
    else
    {
        kind->addItems ({ tr ("Sine"), tr ("Square"), tr ("Sawtooth"), tr ("Triangle") });
        kind->setCurrentIndex (settings.value (QStringLiteral ("wave"), 0).toInt());
        form->addRow (tr ("&Waveform:"), kind);
    }

    auto* frequency = spin (&dialog, 1.0, 96000.0, 2, tr (" Hz"), settings.value (QStringLiteral ("frequency"), 440.0).toDouble());
    auto* sweep = new QCheckBox (tr ("S&weep to"), &dialog);
    sweep->setChecked (settings.value (QStringLiteral ("sweep"), false).toBool());
    auto* endFrequency = spin (&dialog, 1.0, 96000.0, 2, tr (" Hz"), settings.value (QStringLiteral ("endFrequency"), 4000.0).toDouble());
    endFrequency->setToolTip (tr ("The pitch glides evenly (by octaves) from the first frequency to this one"));
    auto* pulse = spin (&dialog, 1.0, 99.0, 0, tr (" %"), settings.value (QStringLiteral ("pulseWidth"), 50.0).toDouble());
    pulse->setToolTip (tr ("How much of each cycle the square wave is high: 50% is a true square, smaller is a thin pulse"));
    auto* bandLimited = new QCheckBox (tr ("&Band-limited (no aliasing)"), &dialog);
    bandLimited->setChecked (settings.value (QStringLiteral ("bandLimited"), true).toBool());
    bandLimited->setToolTip (tr ("On: no harmonics above half the sample rate, as a good synthesizer makes it. "
                                 "Off: the raw shape, whose harmonics fold back as audible aliasing (useful for teaching)."));
    if (! makeNoise)
    {
        auto* sweepRow = new QHBoxLayout;
        sweepRow->addWidget (sweep);
        sweepRow->addWidget (endFrequency, 1);
        form->addRow (tr ("&Frequency:"), frequency);
        form->addRow (QString(), sweepRow);
        form->addRow (tr ("&Pulse width:"), pulse);
        form->addRow (QString(), bandLimited);
    }
    else
        for (QWidget* w : { (QWidget*) frequency, (QWidget*) sweep, (QWidget*) endFrequency, (QWidget*) pulse, (QWidget*) bandLimited })
            w->hide();

    auto* level = spin (&dialog, -96.0, 0.0, 1, tr (" dBFS"), settings.value (makeNoise ? QStringLiteral ("noiseLevel") : QStringLiteral ("level"), -6.0).toDouble());
    level->setToolTip (tr ("The peak level"));
    form->addRow (tr ("&Level:"), level);

    // Length: the selection's when there is one, otherwise the last one used.
    const auto rate = d ? d->sampleRate() : 48000.0;
    const auto selected = d && hasSelection() ? (double) (selEnd - selStart) / rate : 0.0;
    auto* seconds = spin (&dialog, 0.001, 3600.0, 3, tr (" s"), selected > 0.0 ? selected : settings.value (QStringLiteral ("seconds"), 5.0).toDouble());
    form->addRow (tr ("&Duration:"), seconds);

    auto* different = new QCheckBox (tr ("Different noise in each &channel"), &dialog);
    different->setChecked (settings.value (QStringLiteral ("decorrelated"), true).toBool());
    different->setToolTip (tr ("On: wide stereo noise. Off: the same noise in every channel (mono, in the centre)."));
    if (makeNoise)
        form->addRow (QString(), different);
    else
        different->hide();

    auto* where = new QLabel (d == nullptr ? tr ("Nothing is open, so this makes a new stereo file at 48 kHz.")
                              : hasSelection() ? tr ("Replaces the selection.") : tr ("Inserted at the cursor."), &dialog);
    where->setForegroundRole (QPalette::PlaceholderText);

    auto refresh = [&]
    {
        if (makeNoise)
            return;
        endFrequency->setEnabled (sweep->isChecked());
        pulse->setEnabled (kind->currentIndex() == 1);
        bandLimited->setEnabled (kind->currentIndex() != 0);
    };
    connect (kind, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect (sweep, &QCheckBox::toggled, &dialog, refresh);
    refresh();

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout (&dialog);
    layout->addLayout (form);
    layout->addWidget (where);
    layout->addWidget (buttons);
    kind->setFocus();

    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue (makeNoise ? QStringLiteral ("noise") : QStringLiteral ("wave"), kind->currentIndex());
    settings.setValue (makeNoise ? QStringLiteral ("noiseLevel") : QStringLiteral ("level"), level->value());
    if (selected <= 0.0)
        settings.setValue (QStringLiteral ("seconds"), seconds->value());
    if (makeNoise)
        settings.setValue (QStringLiteral ("decorrelated"), different->isChecked());
    else
    {
        settings.setValue (QStringLiteral ("frequency"), frequency->value());
        settings.setValue (QStringLiteral ("sweep"), sweep->isChecked());
        settings.setValue (QStringLiteral ("endFrequency"), endFrequency->value());
        settings.setValue (QStringLiteral ("pulseWidth"), pulse->value());
        settings.setValue (QStringLiteral ("bandLimited"), bandLimited->isChecked());
    }

    const auto channels = d ? d->numChannels() : 2;
    const auto frames = std::max<Frame> (1, (Frame) std::llround (seconds->value() * rate));
    std::vector<std::vector<float>> signal ((size_t) channels);
    QString title;
    if (makeNoise)
    {
        const auto colour = (gen::Noise) kind->currentIndex();
        const auto seed = std::random_device{}();
        for (int ch = 0; ch < channels; ++ch)
            signal[(size_t) ch] = ch > 0 && ! different->isChecked() ? signal[0]
                                : gen::noise (colour, level->value(), rate, frames, seed + (std::uint32_t) ch);
        title = tr ("Generate %1 Noise").arg (kind->currentText().section (QLatin1Char (' '), 0, 0));
    }
    else
    {
        gen::Tone tone;
        tone.wave = (gen::Wave) kind->currentIndex();
        tone.startHz = frequency->value();
        tone.endHz = sweep->isChecked() ? endFrequency->value() : frequency->value();
        tone.peakDb = level->value();
        tone.bandLimited = bandLimited->isChecked();
        tone.pulseWidth = pulse->value() / 100.0;
        signal[0] = gen::tone (tone, rate, frames);
        for (int ch = 1; ch < channels; ++ch)
            signal[(size_t) ch] = signal[0];
        title = sweep->isChecked() ? tr ("Generate %1 Sweep").arg (kind->currentText()) : tr ("Generate %1 Tone").arg (kind->currentText());
    }

    AudioData::Builder builder (channels, rate);
    std::vector<const float*> ptrs;
    for (const auto& c : signal)
        ptrs.push_back (c.data());
    builder.append (ptrs.data(), (int) frames);
    const auto audio = builder.finish();

    if (d == nullptr)
    {
        // Into the empty tab when there is one.
        if (tabs.empty())
            newTab();
        doc.reset (audio, {}, {});
        doc.markUnsaved();
        refreshFromDocument (false);
    }
    else
    {
        const auto start = hasSelection() ? selStart : cursor, end = hasSelection() ? selEnd : cursor;
        selStart = selAnchor = start;
        selEnd = cursor = start + frames;
        commit (title, d->replaced (start, end, audio.get()), start, end, frames);
    }
    statusBar()->showMessage (title, 4000);
    waveform->setFocus();
}
