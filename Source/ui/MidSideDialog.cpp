// SPDX-License-Identifier: AGPL-3.0-only
#include "MidSideDialog.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int dots = 5000;

    // Up to `dots` (side, mid) pairs spread through the sound.
    std::vector<QPointF> fieldPoints (const AudioData& d)
    {
        std::vector<QPointF> points;
        if (d.length() == 0)
            return points;
        const auto step = std::max<Frame> (1, d.length() / dots);
        const bool stereo = d.numChannels() > 1;
        std::vector<float*> ptrs;
        std::vector<std::vector<float>> one ((size_t) d.numChannels(), std::vector<float> (1));
        for (auto& c : one)
            ptrs.push_back (c.data());
        for (Frame f = 0; f < d.length(); f += step)
        {
            d.read (f, 1, ptrs.data());
            const auto left = one[0][0], right = one[stereo ? 1 : 0][0];
            points.emplace_back (0.5 * (left - right), 0.5 * (left + right));
        }
        return points;
    }

    QString signedText (double v, int decimals) { return (v >= 0.0 ? QStringLiteral ("+") : QString()) + QString::number (v, 'f', decimals); }
}

//==============================================================================
MidSideField::MidSideField (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::StrongFocus);
    setMinimumSize (220, 220);
    setToolTip (tr ("Drag sideways for width, up or down to rotate. Keys: Left/Right width, Up/Down rotation, Shift finer, Home resets."));
}

void MidSideField::setPoints (std::vector<QPointF> b, std::vector<QPointF> a)
{
    before = std::move (b);
    after = std::move (a);
    update();
}

void MidSideField::setValues (double w, double top, double r)
{
    amount = w;
    most = top;
    rotation = r;
    update();
}

void MidSideField::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    const auto side = std::min (width(), height()) - 8.0;
    const QRectF area ((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const auto c = area.center();
    const auto R = side / 2.0;
    const auto line = theme.color (QStringLiteral ("mid"));
    const auto text = theme.color (QStringLiteral ("placeholderText"));

    // Axes: mono straight up, the left and right channels on the diagonals.
    g.setPen (QPen (line, 1.0, Qt::DotLine));
    g.drawLine (QPointF (c.x(), area.top()), QPointF (c.x(), area.bottom()));
    g.drawLine (QPointF (area.left(), c.y()), QPointF (area.right(), c.y()));
    g.setPen (QPen (line, 1.0));
    const auto d = R * M_SQRT1_2;
    g.drawLine (QPointF (c.x() - d, c.y() - d), QPointF (c.x() + d, c.y() + d));
    g.drawLine (QPointF (c.x() + d, c.y() - d), QPointF (c.x() - d, c.y() + d));
    g.setPen (text);
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.85);
    g.setFont (small);
    g.drawText (QPointF (c.x() - d - 4, c.y() - d - 4), tr ("L"));
    g.drawText (QPointF (c.x() + d - 4, c.y() - d - 4), tr ("R"));
    g.drawText (QPointF (c.x() + 4, area.top() + 12), tr ("mono"));
    g.drawText (QPointF (area.left() + 2, c.y() - 4), tr ("side"));

    auto plot = [&] (const std::vector<QPointF>& points, QColor colour)
    {
        g.setPen (Qt::NoPen);
        g.setBrush (colour);
        for (const auto& p : points)
            g.drawEllipse (QPointF (c.x() + std::clamp (p.x(), -1.0, 1.0) * R, c.y() - std::clamp (p.y(), -1.0, 1.0) * R), 1.1, 1.1);
    };
    auto grey = text;
    grey.setAlpha (70);
    plot (before, grey);
    auto colour = theme.color (QStringLiteral ("scope"));
    colour.setAlpha (150);
    plot (after, colour);

    // The width as a bar along the bottom, the rotation as a tick on the top edge.
    const auto accent = theme.color (QStringLiteral ("highlight"));
    const auto share = amount / most;
    g.setPen (QPen (accent, hasFocus() ? 3.0 : 2.0));
    g.drawLine (QPointF (c.x() - R * share, area.bottom() - 2), QPointF (c.x() + R * share, area.bottom() - 2));
    const auto tilt = rotation / 45.0 * R * 0.5;
    g.drawLine (QPointF (c.x() + tilt, area.top()), QPointF (c.x() + tilt, area.top() + 8));
    g.setPen (theme.color (QStringLiteral ("windowText")));
    g.drawText (QRectF (area.left(), area.bottom() - 20, side, 16), Qt::AlignCenter,
                tr ("%1 %").arg (std::lround (amount * 100.0)) + (std::abs (rotation) > 0.05 ? tr ("   %1°").arg (signedText (rotation, 1)) : QString()));
}

void MidSideField::mousePressEvent (QMouseEvent* e)
{
    pressAt = e->position();
    pressAmount = amount;
    pressRotation = rotation;
    setFocus();
}

void MidSideField::mouseMoveEvent (QMouseEvent* e)
{
    if (! (e->buttons() & Qt::LeftButton))
        return;
    const auto side = std::max (1.0, (double) std::min (width(), height()));
    const auto delta = e->position() - pressAt;
    // Across the whole field is the whole width range; up the field is 45 degrees.
    const auto w = std::clamp (pressAmount + delta.x() / side * most, 0.0, most);
    const auto r = std::clamp (pressRotation - delta.y() / side * 90.0, -45.0, 45.0);
    Q_EMIT changed (std::round (w * 100.0) / 100.0, std::round (r * 2.0) / 2.0);
}

void MidSideField::keyPressEvent (QKeyEvent* e)
{
    const bool fine = e->modifiers() & Qt::ShiftModifier;
    switch (e->key())
    {
        case Qt::Key_Left:  Q_EMIT changed (std::clamp (amount - (fine ? 0.01 : 0.05), 0.0, most), rotation); return;
        case Qt::Key_Right: Q_EMIT changed (std::clamp (amount + (fine ? 0.01 : 0.05), 0.0, most), rotation); return;
        case Qt::Key_Up:    Q_EMIT changed (amount, std::clamp (rotation + (fine ? 0.5 : 5.0), -45.0, 45.0)); return;
        case Qt::Key_Down:  Q_EMIT changed (amount, std::clamp (rotation - (fine ? 0.5 : 5.0), -45.0, 45.0)); return;
        case Qt::Key_Home:  Q_EMIT changed (1.0, 0.0); return;
        default: QWidget::keyPressEvent (e);
    }
}

//==============================================================================
MidSideDialog::MidSideDialog (const midside::Settings& initial, bool wholeFile, AudioDataPtr s,
                              std::function<void (AudioDataPtr)> p, std::function<bool()> f, QWidget* parent)
    : QDialog (parent), sample (std::move (s)), play (std::move (p)), flip (std::move (f))
{
    setWindowTitle (tr ("Mid/Side Width"));

    field = new MidSideField (this);
    presetBox = new QComboBox (this);
    presetBox->addItem (tr ("(your settings)"));
    for (const auto& preset : midside::presets())
        presetBox->addItem (tr (preset.name.c_str()));
    auto spin = [this] (int lo, int hi, int step, const QString& suffix, const QString& tip)
    {
        auto* b = new QSpinBox (this);
        b->setRange (lo, hi);
        b->setSingleStep (step);
        b->setSuffix (suffix);
        b->setAccelerated (true);
        b->setToolTip (tip);
        return b;
    };
    auto real = [this] (double lo, double hi, double step, const QString& suffix, const QString& tip)
    {
        auto* b = new QDoubleSpinBox (this);
        b->setRange (lo, hi);
        b->setSingleStep (step);
        b->setDecimals (1);
        b->setSuffix (suffix);
        b->setAccelerated (true);
        b->setToolTip (tip);
        return b;
    };
    widthBox = spin (0, 200, 5, tr (" %"), tr ("The level of the side: 0% is mono, 100% unchanged, 200% twice as wide"));
    hugeBox = new QCheckBox (tr ("H&uge (up to 300%, backs off when out of phase)"), this);
    hugeBox->setToolTip (tr ("Allows up to 300% width; while the left and right go out of phase (correlation below -0.3) the width eases back"));
    monoBox = spin (0, 500, 10, tr (" Hz"), tr ("Everything below this is made mono, for tight, centred bass (Off leaves it)"));
    monoBox->setSpecialValueText (tr ("Off"));
    generateBox = spin (0, 100, 5, tr (" %"), tr ("Adds width made from the centre above 150 Hz; it is pure side, so it vanishes when played in mono. Widens mono sounds too"));
    airBox = real (-6.0, 6.0, 0.5, tr (" dB"), tr ("A high shelf (8 kHz) on the side only: more sparkle at the edges without touching the centre"));
    rotationBox = real (-45.0, 45.0, 1.0, tr ("°"), tr ("Turns the whole image; positive moves it to the right"));
    liftBox = spin (0, 100, 5, tr (" %"), tr ("Widens louder moments more (up to twice), so choruses and hits open up"));
    matchBox = new QCheckBox (tr ("&Match level"), this);
    matchBox->setToolTip (tr ("Keeps the result as loud as the original, so you hear the width and not just more level"));
    goldenBox = new QCheckBox (tr ("Φ &golden ratio for generated sides"), this);
    goldenBox->setToolTip (tr ("Generate sides makes its width with four allpasses spaced by the golden ratio instead of one delay, "
                               "so the colour it adds is uneven and less pitched. Still vanishes in mono"));
    listenBox = new QComboBox (this);
    listenBox->addItems ({ tr ("Stereo"), tr ("Mono (check it still works summed)"), tr ("Side only"), tr ("Bass (below the mono crossover)") });
    listenBox->setToolTip (tr ("What the preview plays; the result is always stereo"));

    readout = new QLabel (this);
    readout->setWordWrap (true);
    readout->setTextFormat (Qt::RichText);
    previewBox = new QCheckBox (tr ("&Preview"), this);
    previewBox->setToolTip (tr ("Plays the result in a loop and follows your changes"));
    abButton = new QPushButton (tr ("&A/B"), this);
    abButton->setAutoDefault (false);
    abButton->setEnabled (false);
    abButton->setToolTip (tr ("While previewing, switches between the result and the original"));

    auto* form = new QFormLayout;
    form->addRow (tr ("Pr&eset:"), presetBox);
    form->addRow (tr ("&Width:"), widthBox);
    form->addRow (QString(), hugeBox);
    form->addRow (tr ("M&ono below:"), monoBox);
    form->addRow (tr ("&Generate sides:"), generateBox);
    form->addRow (QString(), goldenBox);
    form->addRow (tr ("A&ir:"), airBox);
    form->addRow (tr ("&Rotation:"), rotationBox);
    form->addRow (tr ("&Lift:"), liftBox);
    form->addRow (QString(), matchBox);
    form->addRow (tr ("Lis&ten:"), listenBox);

    auto* about = new QLabel (tr ("Widens or narrows the stereo image by changing the side (what differs between left and right) "
                                  "and leaving the centre alone. %1")
                                  .arg (wholeFile ? tr ("It applies to the whole file.") : tr ("It applies to the selection.")), this);
    about->setWordWrap (true);
    auto* columns = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    left->addWidget (field, 1);
    left->addWidget (readout);
    columns->addLayout (left, 1);
    columns->addLayout (form);
    auto* previewRow = new QHBoxLayout;
    previewRow->addWidget (previewBox);
    previewRow->addWidget (abButton);
    previewRow->addStretch (1);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (columns, 1);
    layout->addLayout (previewRow);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (sample != nullptr)
    {
        beforePoints = fieldPoints (*sample);
        beforeAnalysis = midside::analyse (*sample, 0, sample->length());
    }
    load (initial);

    connect (presetBox, &QComboBox::activated, this, [this] (int index)
    {
        if (index > 0)
        {
            auto preset = midside::presets()[(size_t) index - 1].settings;
            preset.golden = goldenBox->isChecked();   // presets leave the Φ switch as it is
            load (preset);
        }
    });
    for (auto* b : { widthBox, monoBox, generateBox, liftBox })
        connect (b, &QSpinBox::valueChanged, this, &MidSideDialog::changed);
    for (auto* b : { airBox, rotationBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &MidSideDialog::changed);
    for (auto* b : { hugeBox, matchBox, goldenBox })
        connect (b, &QCheckBox::toggled, this, &MidSideDialog::changed);
    connect (hugeBox, &QCheckBox::toggled, this, [this] (bool on) { widthBox->setMaximum (on ? 300 : 200); });
    connect (listenBox, &QComboBox::currentIndexChanged, this, &MidSideDialog::changed);
    connect (field, &MidSideField::changed, this, [this] (double w, double r)
    {
        loading = true;
        widthBox->setValue ((int) std::lround (w * 100.0));
        rotationBox->setValue (r);
        loading = false;
        changed();
    });
    connect (previewBox, &QCheckBox::toggled, this, [this] (bool on)
    {
        abButton->setEnabled (on);
        abButton->setText (tr ("&A/B"));
        if (on)
            timer.start (0);
        else if (play)
            play (nullptr);
    });
    connect (abButton, &QPushButton::clicked, this, [this]
    {
        if (flip)
            abButton->setText (flip() ? tr ("&A/B: result") : tr ("&A/B: original"));
    });

    // The result is rendered after a short pause, so dragging stays smooth.
    timer.setSingleShot (true);
    timer.setInterval (120);
    connect (&timer, &QTimer::timeout, this, &MidSideDialog::update);
    update();
    field->setFocus();
}

MidSideDialog::~MidSideDialog() = default;

void MidSideDialog::load (const midside::Settings& s)
{
    loading = true;
    hugeBox->setChecked (s.huge);
    widthBox->setMaximum (s.huge ? 300 : 200);
    widthBox->setValue ((int) std::lround (s.width * 100.0));
    monoBox->setValue ((int) std::lround (s.monoBelowHz));
    generateBox->setValue ((int) std::lround (s.generate * 100.0));
    airBox->setValue (s.airDb);
    rotationBox->setValue (s.rotation);
    liftBox->setValue ((int) std::lround (s.lift * 100.0));
    matchBox->setChecked (s.matchLevel);
    goldenBox->setChecked (s.golden);
    loading = false;
    changed();
}

midside::Settings MidSideDialog::settings() const
{
    midside::Settings s;
    s.width = widthBox->value() / 100.0;
    s.huge = hugeBox->isChecked();
    s.monoBelowHz = monoBox->value();
    s.generate = generateBox->value() / 100.0;
    s.airDb = airBox->value();
    s.rotation = rotationBox->value();
    s.lift = liftBox->value() / 100.0;
    s.matchLevel = matchBox->isChecked();
    s.golden = goldenBox->isChecked();
    return s;
}

void MidSideDialog::changed()
{
    if (loading)
        return;
    const auto s = settings();
    field->setValues (s.width, s.maxWidth(), s.rotation);
    // The preset name stays only while the settings match it.
    const auto index = presetBox->currentIndex();
    if (index > 0)
    {
        const auto& p = midside::presets()[(size_t) index - 1].settings;
        const bool same = std::abs (p.width - s.width) < 0.005 && p.huge == s.huge && std::abs (p.monoBelowHz - s.monoBelowHz) < 0.5
                       && std::abs (p.generate - s.generate) < 0.005 && std::abs (p.airDb - s.airDb) < 0.05
                       && std::abs (p.rotation - s.rotation) < 0.05 && std::abs (p.lift - s.lift) < 0.005 && p.matchLevel == s.matchLevel;
        if (! same)
            presetBox->setCurrentIndex (0);
    }
    timer.start();
}

void MidSideDialog::update()
{
    if (sample == nullptr)
        return;
    const auto s = settings();
    const auto result = midside::render (*sample, 0, sample->length(), s);
    const auto after = midside::analyse (*result, 0, result->length());

    auto points = fieldPoints (*result);
    double most = 1.0e-6;
    for (const auto* list : { &beforePoints, &points })
        for (const auto& p : *list)
            most = std::max ({ most, std::abs (p.x()), std::abs (p.y()) });
    auto scaled = [most] (std::vector<QPointF> list)
    {
        for (auto& p : list)
            p /= most;
        return list;
    };
    field->setPoints (scaled (beforePoints), scaled (std::move (points)));

    auto colourFor = [] (double correlation)
    {
        const auto& theme = Theme::current();
        return theme.color (correlation < -0.3 ? QStringLiteral ("levelPeak") : correlation < 0.0 ? QStringLiteral ("levelHot") : QStringLiteral ("windowText")).name();
    };
    auto side = [] (double db) { return db < -60.0 ? tr ("none") : tr ("%1 dB").arg (db, 0, 'f', 1); };
    readout->setText (tr ("<b>Correlation</b> <span style='color:%1'>%2</span> (was %3)<br><b>Mono sum</b> %4 dB (was %5 dB)<br><b>Side against centre</b> %6 (was %7)")
                          .arg (colourFor (after.correlation), signedText (after.correlation, 2), signedText (beforeAnalysis.correlation, 2))
                          .arg (after.monoSumDb, 0, 'f', 1).arg (beforeAnalysis.monoSumDb, 0, 'f', 1)
                          .arg (side (after.sideDb), side (beforeAnalysis.sideDb)));

    if (previewBox->isChecked() && play)
    {
        if (listenBox->currentIndex() == 0)
            play (result);
        else
        {
            auto heard = s;
            heard.listen = (midside::Listen) listenBox->currentIndex();
            play (midside::render (*sample, 0, sample->length(), heard));
        }
    }
}

void MidSideDialog::done (int result)
{
    timer.stop();
    if (previewBox->isChecked() && play)
        play (nullptr);
    QDialog::done (result);
}
