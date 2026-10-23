// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Ambisonics.h"

#include <QDialog>
#include <QTimer>
#include <functional>

class BinauralField;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

// Spatial > Encode to Ambisonics: where a mono or stereo sound sits in the sphere, and the order.
class AmbiEncodeDialog final : public QDialog
{
    Q_OBJECT

public:
    AmbiEncodeDialog (const ambi::EncodeSettings& initial, bool stereo, QWidget* parent);
    ambi::EncodeSettings settings() const;

private:
    void changed();

    BinauralField* field;
    QComboBox* orderBox;
    QDoubleSpinBox* azimuthBox;
    QDoubleSpinBox* elevationBox;
    QDoubleSpinBox* widthBox = nullptr;
    bool updating = false;
};

// Spatial > Rotate / Mirror: turns or flips the whole scene, with a preview.
class AmbiRotateDialog final : public QDialog
{
    Q_OBJECT

public:
    struct Choice
    {
        double yaw = 0.0, pitch = 0.0, roll = 0.0;
        bool leftRight = false, frontBack = false, upDown = false;
    };

    // `preview` gets the matrix while Preview is on (after each change), and nothing when it goes off.
    AmbiRotateDialog (int order, bool wholeFile, std::function<void (const ambi::Matrix*)> preview, QWidget* parent);
    ~AmbiRotateDialog() override;

    Choice choice() const;
    ambi::Matrix matrix() const;   // the rotation, then the mirror
    void done (int result) override;

private:
    void changed();

    int order;
    QDoubleSpinBox* yawBox;
    QDoubleSpinBox* pitchBox;
    QDoubleSpinBox* rollBox;
    QCheckBox* leftRightBox;
    QCheckBox* frontBackBox;
    QCheckBox* upDownBox;
    QCheckBox* previewBox;
    std::function<void (const ambi::Matrix*)> preview;
    QTimer previewTimer;
};

// Spatial > Decode Ambisonics: to headphones (through an HRTF set) or to a speaker layout.
class AmbiDecodeDialog final : public QDialog
{
    Q_OBJECT

public:
    AmbiDecodeDialog (const QString& initialTarget, const QStringList& hrtfFiles, const QString& initialHrtf, QWidget* parent);
    QString target() const;   // "Headphones", a name from ambi::layoutNames(), or "Custom: " and one of your arrays
    QString hrtf() const;

private:
    QComboBox* targetBox;
    QComboBox* hrtfBox;
};
