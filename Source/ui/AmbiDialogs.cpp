// SPDX-License-Identifier: AGPL-3.0-only
#include "AmbiDialogs.h"
#include "BinauralDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    QDoubleSpinBox* degrees (QWidget* parent, double lo, double hi, const QString& tip)
    {
        auto* b = new QDoubleSpinBox (parent);
        b->setRange (lo, hi);
        b->setSingleStep (5.0);
        b->setDecimals (0);
        b->setSuffix (QObject::tr ("°"));
        b->setAccelerated (true);
        b->setToolTip (tip);
        return b;
    }

    QString orderName (int order)
    {
        return order == 1 ? QObject::tr ("1st order (4 channels)")
             : order == 2 ? QObject::tr ("2nd order (9 channels)")
                          : QObject::tr ("3rd order (16 channels)");
    }
}

//==============================================================================
AmbiEncodeDialog::AmbiEncodeDialog (const ambi::EncodeSettings& initial, bool stereo, QWidget* parent) : QDialog (parent)
{
    setWindowTitle (tr ("Encode to Ambisonics"));

    field = new BinauralField (stereo, this, false);
    orderBox = new QComboBox (this);
    for (int order = 1; order <= 3; ++order)
        orderBox->addItem (orderName (order), order);
    orderBox->setCurrentIndex (std::clamp (initial.order, 1, 3) - 1);
    orderBox->setToolTip (tr ("Higher orders place sounds more sharply but take more channels"));

    azimuthBox = degrees (this, -180.0, 180.0, tr ("0° is in front, 90° right, -90° left, 180° behind"));
    azimuthBox->setWrapping (true);
    elevationBox = degrees (this, -90.0, 90.0, tr ("0° is at ear height, 90° straight above"));
    if (stereo)
        widthBox = degrees (this, 0.0, 180.0, tr ("How far apart the left and right channels are"));
    azimuthBox->setValue (initial.azimuth);
    elevationBox->setValue (initial.elevation);
    if (widthBox != nullptr)
        widthBox->setValue (initial.width);

    auto* form = new QFormLayout;
    form->addRow (tr ("&Order:"), orderBox);
    form->addRow (tr ("&Azimuth:"), azimuthBox);
    form->addRow (tr ("&Elevation:"), elevationBox);
    if (widthBox != nullptr)
        form->addRow (tr ("&Width:"), widthBox);

    auto* about = new QLabel (stereo ? tr ("Places this stereo sound in an ambisonic scene (AmbiX): its two channels become two sources. "
                                           "The whole file becomes ambisonic.")
                                     : tr ("Places this mono sound in an ambisonic scene (AmbiX). The whole file becomes ambisonic."), this);
    about->setWordWrap (true);

    auto* columns = new QHBoxLayout;
    columns->addWidget (field, 1);
    columns->addLayout (form);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (columns, 1);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect (field, &BinauralField::moved, this, [this] (double az, double el, double, double width)
    {
        updating = true;
        azimuthBox->setValue (az);
        elevationBox->setValue (el);
        if (widthBox != nullptr)
            widthBox->setValue (width);
        updating = false;
    });
    for (auto* b : { azimuthBox, elevationBox, widthBox })
        if (b != nullptr)
            connect (b, &QDoubleSpinBox::valueChanged, this, [this] { if (! updating) changed(); });
    changed();
    field->setFocus();
}

void AmbiEncodeDialog::changed()
{
    binaural::Settings s;
    s.azimuth = azimuthBox->value();
    s.elevation = elevationBox->value();
    s.width = widthBox != nullptr ? widthBox->value() : 60.0;
    field->set (s);
}

ambi::EncodeSettings AmbiEncodeDialog::settings() const
{
    ambi::EncodeSettings s;
    s.order = orderBox->currentData().toInt();
    s.azimuth = azimuthBox->value();
    s.elevation = elevationBox->value();
    s.width = widthBox != nullptr ? widthBox->value() : 60.0;
    return s;
}

//==============================================================================
AmbiRotateDialog::AmbiRotateDialog (int o, bool wholeFile, std::function<void (const ambi::Matrix*)> p, QWidget* parent)
    : QDialog (parent), order (o), preview (std::move (p))
{
    setWindowTitle (tr ("Rotate / Mirror Scene"));

    yawBox = degrees (this, -180.0, 180.0, tr ("Turns the scene to the right (negative: to the left)"));
    yawBox->setWrapping (true);
    pitchBox = degrees (this, -180.0, 180.0, tr ("Tilts the front of the scene up (negative: down)"));
    rollBox = degrees (this, -180.0, 180.0, tr ("Lifts the left side of the scene (negative: the right side)"));
    leftRightBox = new QCheckBox (tr ("Swap &left and right"), this);
    frontBackBox = new QCheckBox (tr ("Swap &front and back"), this);
    upDownBox = new QCheckBox (tr ("Swap &up and down"), this);
    previewBox = new QCheckBox (tr ("&Preview"), this);
    previewBox->setToolTip (tr ("Plays the result in a loop and follows your changes"));

    auto* form = new QFormLayout;
    form->addRow (tr ("&Yaw (turn):"), yawBox);
    form->addRow (tr ("P&itch (tilt):"), pitchBox);
    form->addRow (tr ("&Roll:"), rollBox);
    form->addRow (leftRightBox);
    form->addRow (frontBackBox);
    form->addRow (upDownBox);

    auto* about = new QLabel (tr ("Turns or flips the whole ambisonic scene (%1). It applies to %2.")
                                  .arg (orderName (order), wholeFile ? tr ("the whole file") : tr ("the selection")), this);
    about->setWordWrap (true);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (form);
    layout->addWidget (previewBox);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    previewTimer.setSingleShot (true);
    previewTimer.setInterval (150);
    connect (&previewTimer, &QTimer::timeout, this, [this]
    {
        if (! previewBox->isChecked() || ! preview)
            return;
        const auto m = matrix();
        preview (&m);
    });
    for (auto* b : { yawBox, pitchBox, rollBox })
        connect (b, &QDoubleSpinBox::valueChanged, this, &AmbiRotateDialog::changed);
    for (auto* b : { leftRightBox, frontBackBox, upDownBox })
        connect (b, &QCheckBox::toggled, this, &AmbiRotateDialog::changed);
    connect (previewBox, &QCheckBox::toggled, this, [this] (bool on)
    {
        if (on)
            previewTimer.start (0);
        else if (preview)
            preview (nullptr);
    });
    yawBox->setFocus();
}

AmbiRotateDialog::~AmbiRotateDialog() = default;

void AmbiRotateDialog::changed()
{
    if (previewBox->isChecked())
        previewTimer.start();
}

void AmbiRotateDialog::done (int result)
{
    previewTimer.stop();
    if (previewBox->isChecked() && preview)
        preview (nullptr);
    QDialog::done (result);
}

AmbiRotateDialog::Choice AmbiRotateDialog::choice() const
{
    return { yawBox->value(), pitchBox->value(), rollBox->value(),
             leftRightBox->isChecked(), frontBackBox->isChecked(), upDownBox->isChecked() };
}

ambi::Matrix AmbiRotateDialog::matrix() const
{
    const auto c = choice();
    const auto r = ambi::rotation (order, c.yaw, c.pitch, c.roll);
    const auto m = ambi::mirror (order, c.leftRight, c.frontBack, c.upDown);
    const auto k = r.size();
    ambi::Matrix out (k, std::vector<double> (k, 0.0));
    for (size_t i = 0; i < k; ++i)
        for (size_t j = 0; j < k; ++j)
            for (size_t n = 0; n < k; ++n)
                out[i][j] += m[i][n] * r[n][j];
    return out;
}

//==============================================================================
AmbiDecodeDialog::AmbiDecodeDialog (const QString& initialTarget, const QStringList& hrtfFiles, const QString& initialHrtf, QWidget* parent)
    : QDialog (parent)
{
    setWindowTitle (tr ("Decode Ambisonics"));

    targetBox = new QComboBox (this);
    targetBox->addItem (tr ("Headphones (binaural)"), QStringLiteral ("Headphones"));
    for (const auto& name : ambi::layoutNames())
    {
        const auto n = ambi::layout (name).size();
        targetBox->addItem (tr ("%1 speakers (%2 channels)").arg (QString::fromStdString (name)).arg (n), QString::fromStdString (name));
    }
    targetBox->setCurrentIndex (std::max (0, targetBox->findData (initialTarget)));

    hrtfBox = new QComboBox (this);
    for (const auto& f : hrtfFiles)
        hrtfBox->addItem (QFileInfo (f).completeBaseName(), f);
    if (! initialHrtf.isEmpty() && hrtfBox->findData (initialHrtf) < 0)
        hrtfBox->addItem (QFileInfo (initialHrtf).completeBaseName(), initialHrtf);
    hrtfBox->setCurrentIndex (std::max (0, hrtfBox->findData (initialHrtf)));
    auto* otherHrtf = new QPushButton (tr ("Other &File..."), this);
    otherHrtf->setAutoDefault (false);
    connect (otherHrtf, &QPushButton::clicked, this, [this]
    {
        const auto path = QFileDialog::getOpenFileName (this, tr ("HRTF Set"), {}, tr ("SOFA files (*.sofa)"));
        if (path.isEmpty())
            return;
        if (hrtfBox->findData (path) < 0)
            hrtfBox->addItem (QFileInfo (path).completeBaseName(), path);
        hrtfBox->setCurrentIndex (hrtfBox->findData (path));
    });
    auto* hrtfRow = new QHBoxLayout;
    hrtfRow->addWidget (hrtfBox, 1);
    hrtfRow->addWidget (otherHrtf);

    auto* form = new QFormLayout;
    form->addRow (tr ("&Decode to:"), targetBox);
    auto* hrtfLabel = new QLabel (tr ("&HRTF set:"), this);
    hrtfLabel->setBuddy (hrtfBox);
    form->addRow (hrtfLabel, hrtfRow);
    const auto headphones = [this, hrtfLabel, otherHrtf]
    {
        const bool on = targetBox->currentData().toString() == QStringLiteral ("Headphones");
        hrtfBox->setEnabled (on);
        otherHrtf->setEnabled (on);
        hrtfLabel->setEnabled (on);
    };
    connect (targetBox, &QComboBox::currentIndexChanged, this, headphones);
    headphones();

    auto* about = new QLabel (tr ("Turns the whole ambisonic scene into speaker feeds (AllRAD with max-rE) or into stereo for headphones. "
                                  "Subwoofer channels stay silent: add bass management in your speaker system."), this);
    about->setWordWrap (true);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (form);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    targetBox->setFocus();
}

QString AmbiDecodeDialog::target() const { return targetBox->currentData().toString(); }
QString AmbiDecodeDialog::hrtf() const { return hrtfBox->currentData().toString(); }
