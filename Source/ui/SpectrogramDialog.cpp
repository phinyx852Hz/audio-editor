// SPDX-License-Identifier: AGPL-3.0-only
#include "SpectrogramDialog.h"
#include "WaveformView.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace
{
    QComboBox* combo (QWidget* parent, const QStringList& items, const QList<int>& values)
    {
        auto* box = new QComboBox (parent);
        for (int i = 0; i < items.size(); ++i)
            box->addItem (items[i], values.value (i, i));
        return box;
    }

    void select (QComboBox* box, int value)
    {
        const auto i = box->findData (value);
        box->setCurrentIndex (i >= 0 ? i : 0);
    }
}

SpectrogramDialog::SpectrogramDialog (WaveformView& v, QWidget* parent) : QDialog (parent), view (v)
{
    setWindowTitle (tr ("Spectrogram Settings"));

    QStringList sizes;
    QList<int> sizeValues;
    for (int s = 256; s <= 32768; s *= 2)
    {
        sizes << QString::number (s);
        sizeValues << s;
    }
    windowSize = combo (this, sizes, sizeValues);
    windowSize->setToolTip (tr ("Larger windows separate frequencies more finely; smaller ones place sounds more exactly in time"));
    windowShape = combo (this, { tr ("Hann"), tr ("Hamming"), tr ("Blackman-Harris") }, { 0, 1, 2 });
    windowShape->setToolTip (tr ("Blackman-Harris keeps quiet sounds next to loud ones visible; Hann is the usual choice"));
    overlap = combo (this, { tr ("None"), tr ("50%"), tr ("75%"), tr ("87.5%"), tr ("93.75%") }, { 1, 2, 4, 8, 16 });
    overlap->setToolTip (tr ("How much the analysis windows overlap when zoomed out: more overlap misses fewer short sounds"));
    oversample = combo (this, { tr ("1x"), tr ("2x"), tr ("4x"), tr ("8x") }, { 1, 2, 4, 8 });
    oversample->setToolTip (tr ("Zero padding: draws the spectrum more smoothly between frequencies (it does not add detail)"));
    scale = combo (this, { tr ("Linear"), tr ("Log"), tr ("Mel") }, { 0, 1, 2 });
    scale->setToolTip (tr ("Log gives each octave the same height; Mel follows how we hear pitch; Linear shows high frequencies in detail"));
    bins = combo (this, { tr ("All Bins"), tr ("Peak Bins"), tr ("Peak Frequencies") }, { 0, 1, 2 });
    bins->setToolTip (tr ("Peak Frequencies draws only the peaks, each at its exact frequency (time-frequency reassignment): "
                          "sharp lines for tones and harmonics, useful for pitch"));
    QStringList mapNames;
    for (const auto& name : spectrum::colourMapNames())
        mapNames << tr (name.c_str());
    colours = combo (this, mapNames, {});
    normalise = combo (this, { tr ("None"), tr ("Each Column"), tr ("Visible Area") }, { 0, 1, 2 });
    normalise->setToolTip (tr ("Each Column makes the loudest frequency of every moment full colour, to see quiet passages"));

    gain = new QDoubleSpinBox (this);
    gain->setRange (-50.0, 50.0);
    gain->setSingleStep (1.0);
    gain->setDecimals (1);
    gain->setSuffix (tr (" dB"));
    range = new QDoubleSpinBox (this);
    range->setRange (20.0, 200.0);
    range->setSingleStep (5.0);
    range->setDecimals (0);
    range->setSuffix (tr (" dB"));
    range->setToolTip (tr ("How far below the top a level is drawn: smaller hides quiet noise"));

    lowHz = new QSpinBox (this);
    highHz = new QSpinBox (this);
    for (auto* box : { lowHz, highHz })
    {
        box->setRange (0, 384000);
        box->setSingleStep (50);
        box->setSuffix (tr (" Hz"));
        box->setAccelerated (true);
        box->setKeyboardTracking (false);
    }
    auto* full = new QPushButton (tr ("&Full Range"), this);
    full->setToolTip (tr ("Every frequency (Alt+0)"));

    auto* analysis = new QGroupBox (tr ("Analysis"), this);
    auto* analysisForm = new QFormLayout (analysis);
    analysisForm->addRow (tr ("&Window size:"), windowSize);
    analysisForm->addRow (tr ("Window &shape:"), windowShape);
    analysisForm->addRow (tr ("&Overlap:"), overlap);
    analysisForm->addRow (tr ("O&versampling:"), oversample);

    auto* display = new QGroupBox (tr ("Display"), this);
    auto* displayForm = new QFormLayout (display);
    displayForm->addRow (tr ("Frequency s&cale:"), scale);
    displayForm->addRow (tr ("&Bins:"), bins);
    displayForm->addRow (tr ("&Colours:"), colours);
    displayForm->addRow (tr ("&Normalise:"), normalise);
    displayForm->addRow (tr ("&Gain:"), gain);
    displayForm->addRow (tr ("&Range:"), range);

    auto* freq = new QGroupBox (tr ("Frequencies Shown"), this);
    auto* freqRow = new QHBoxLayout (freq);
    auto* fromLabel = new QLabel (tr ("Fro&m"), freq);
    fromLabel->setBuddy (lowHz);
    auto* toLabel = new QLabel (tr ("&to"), freq);
    toLabel->setBuddy (highHz);
    freqRow->addWidget (fromLabel);
    freqRow->addWidget (lowHz, 1);
    freqRow->addWidget (toLabel);
    freqRow->addWidget (highHz, 1);
    freqRow->addWidget (full);

    auto* hint = new QLabel (tr ("Alt+wheel over the spectrogram zooms the frequencies around the mouse; "
                                 "Alt+= and Alt+- zoom in and out, Alt+0 shows them all."), this);
    hint->setWordWrap (true);
    hint->setForegroundRole (QPalette::PlaceholderText);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults, this);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect (buttons->button (QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]
    {
        view.setSpectrumSettings ({});
        view.setFrequencyRange (0.0, 0.0);
        refresh();
        Q_EMIT changed();
    });

    auto* columns = new QHBoxLayout;
    columns->addWidget (analysis);
    columns->addWidget (display);
    auto* layout = new QVBoxLayout (this);
    layout->addLayout (columns);
    layout->addWidget (freq);
    layout->addWidget (hint);
    layout->addWidget (buttons);

    for (auto* box : { windowSize, windowShape, overlap, oversample, scale, bins, colours, normalise })
        connect (box, &QComboBox::currentIndexChanged, this, &SpectrogramDialog::apply);
    for (auto* box : { gain, range })
        connect (box, &QDoubleSpinBox::valueChanged, this, &SpectrogramDialog::apply);
    for (auto* box : { lowHz, highHz })
        connect (box, &QSpinBox::valueChanged, this, &SpectrogramDialog::applyRange);
    connect (full, &QPushButton::clicked, this, [this] { view.setFrequencyRange (0.0, 0.0); });
    connect (&view, &WaveformView::frequencyRangeChanged, this, &SpectrogramDialog::refresh);

    refresh();
    windowSize->setFocus();
}

void SpectrogramDialog::refresh()
{
    filling = true;
    const auto& s = view.spectrumSettings();
    select (windowSize, s.windowSize);
    select (windowShape, (int) s.window);
    select (overlap, s.overlap);
    select (oversample, s.oversample);
    select (scale, (int) s.scale);
    select (bins, (int) s.bins);
    colours->setCurrentIndex (s.colourMap);
    select (normalise, (int) s.normalise);
    gain->setValue (s.gainDb);
    range->setValue (s.rangeDb);
    const auto [low, high] = view.frequencyRange();
    lowHz->setValue ((int) std::lround (low));
    highHz->setValue ((int) std::lround (high));
    filling = false;
}

void SpectrogramDialog::apply()
{
    if (filling)
        return;
    spectrum::Settings s;
    s.windowSize = windowSize->currentData().toInt();
    s.window = (spectrum::Window) windowShape->currentData().toInt();
    s.overlap = overlap->currentData().toInt();
    s.oversample = oversample->currentData().toInt();
    s.scale = (spectrum::Scale) scale->currentData().toInt();
    s.bins = (spectrum::Bins) bins->currentData().toInt();
    s.colourMap = colours->currentIndex();
    s.normalise = (spectrum::Normalise) normalise->currentData().toInt();
    s.gainDb = gain->value();
    s.rangeDb = range->value();
    view.setSpectrumSettings (s);
    Q_EMIT changed();
}

void SpectrogramDialog::applyRange()
{
    if (filling || highHz->value() <= lowHz->value() + 10)
        return;
    view.setFrequencyRange (lowHz->value(), highHz->value());
}
