// SPDX-License-Identifier: AGPL-3.0-only
#include "BinauralDialog.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double nearest = 0.5, furthest = 20.0;

    // Distance to a share of the circle's radius, on a log scale: the head takes the middle.
    double radiusFor (double distance)
    {
        return 0.18 + 0.82 * std::log (std::clamp (distance, nearest, furthest) / nearest) / std::log (furthest / nearest);
    }

    double distanceFor (double share)
    {
        return nearest * std::exp ((std::clamp (share, 0.18, 1.0) - 0.18) / 0.82 * std::log (furthest / nearest));
    }

    double wrap (double degrees)
    {
        while (degrees > 180.0) degrees -= 360.0;
        while (degrees <= -180.0) degrees += 360.0;
        return degrees;
    }

    double radians (double degrees) { return degrees * M_PI / 180.0; }
}

//==============================================================================
BinauralField::BinauralField (bool s, QWidget* parent, bool distanceToo) : QWidget (parent), stereo (s), withDistance (distanceToo)
{
    setFocusPolicy (Qt::StrongFocus);
    setMinimumSize (360, 200);
    setAccessibleName (tr ("Sound position"));
    setToolTip (tr ("Drag the sound around the head. Keys: Left/Right turn it, Up/Down closer or further, "
                    "Page Up/Down higher or lower%1.").arg (stereo ? tr (", Shift+Left/Right narrower or wider") : QString()));
}

void BinauralField::set (const binaural::Settings& s)
{
    azimuth = s.azimuth;
    elevation = s.elevation;
    distance = s.distance;
    spread = s.width;
    update();
}

QRectF BinauralField::topArea() const
{
    const auto side = std::min (height() - 8.0, width() * 0.62);
    return { 4.0, (height() - side) / 2.0, side, side };
}

QRectF BinauralField::sideArea() const
{
    const auto top = topArea();
    const auto left = top.right() + 12.0;
    const auto side = std::min (width() - left - 4.0, top.height());
    return { left, top.center().y() - side / 2.0, side, side };
}

void BinauralField::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));

    const auto line = theme.color (QStringLiteral ("mid"));
    const auto text = theme.color (QStringLiteral ("placeholderText"));
    const auto sound = theme.color (QStringLiteral ("scope"));
    const auto accent = theme.color (QStringLiteral ("highlight"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.85);
    g.setFont (small);

    // From above: rings at 1, 3 and 10 m, the head with its nose and ears.
    const auto top = topArea();
    const auto c = top.center();
    const auto R = top.width() / 2.0 - 14.0;
    g.setBrush (Qt::NoBrush);
    for (const double ring : { 1.0, 3.0, 10.0 })
    {
        if (! withDistance)
            break;
        const auto r = R * radiusFor (ring);
        g.setPen (QPen (line, 1.0, ring == 1.0 ? Qt::SolidLine : Qt::DotLine));
        g.drawEllipse (c, r, r);
        g.setPen (text);
        g.drawText (QPointF (c.x() + 3, c.y() - r - 2), tr ("%1 m").arg (ring));
    }
    g.setPen (QPen (line, 1.0));
    g.drawEllipse (c, R, R);
    const auto head = R * 0.1;
    g.setPen (QPen (theme.color (QStringLiteral ("windowText")), 1.5));
    g.drawEllipse (c, head, head);
    g.drawLine (QPointF (c.x() - head * 0.35, c.y() - head * 0.92), QPointF (c.x(), c.y() - head * 1.45));
    g.drawLine (QPointF (c.x() + head * 0.35, c.y() - head * 0.92), QPointF (c.x(), c.y() - head * 1.45));
    g.drawEllipse (QPointF (c.x() - head * 1.05, c.y()), head * 0.18, head * 0.35);
    g.drawEllipse (QPointF (c.x() + head * 1.05, c.y()), head * 0.18, head * 0.35);
    g.setPen (text);
    g.drawText (QRectF (c.x() - 40, top.top() - 2, 80, 14), Qt::AlignCenter, tr ("front"));
    g.drawText (QRectF (c.x() - 40, top.bottom() - 12, 80, 14), Qt::AlignCenter, tr ("behind"));
    g.drawText (QRectF (top.left() - 2, c.y() - 7, 30, 14), Qt::AlignLeft | Qt::AlignVCenter, tr ("L"));
    g.drawText (QRectF (top.right() - 28, c.y() - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, tr ("R"));

    const auto r = withDistance ? R * radiusFor (distance) : R * 0.75;
    const auto pointAt = [&] (double az) { return QPointF (c.x() + r * std::sin (radians (az)), c.y() - r * std::cos (radians (az))); };
    const bool focused = hasFocus();
    if (stereo)
    {
        // The stereo sound as an arc from its left to its right channel.
        const auto half = spread / 2.0;
        QPainterPath arc;
        arc.arcMoveTo (QRectF (c.x() - r, c.y() - r, 2 * r, 2 * r), 90.0 - (azimuth - half));
        arc.arcTo (QRectF (c.x() - r, c.y() - r, 2 * r, 2 * r), 90.0 - (azimuth - half), -spread);
        auto band = sound;
        band.setAlpha (110);
        g.setPen (QPen (band, 6.0, Qt::SolidLine, Qt::RoundCap));
        g.drawPath (arc);
        for (const auto& [az, name] : { std::pair { azimuth - half, tr ("L") }, std::pair { azimuth + half, tr ("R") } })
        {
            const auto p = pointAt (az);
            g.setPen (QPen (focused ? accent : sound.darker (150), 1.5));
            g.setBrush (sound);
            g.drawEllipse (p, 7.0, 7.0);
            g.setPen (theme.color (QStringLiteral ("waveformBackground")));
            auto bold = small;
            bold.setBold (true);
            g.setFont (bold);
            g.drawText (QRectF (p.x() - 7, p.y() - 7, 14, 14), Qt::AlignCenter, name);
            g.setFont (small);
        }
    }
    else
    {
        g.setPen (QPen (focused ? accent : sound.darker (150), 2.0));
        g.setBrush (sound);
        g.drawEllipse (pointAt (azimuth), 8.0, 8.0);
    }

    // From the side, facing right: the height.
    const auto side = sideArea();
    if (side.width() > 60.0)
    {
        const auto sc = side.center();
        const auto sr = side.width() / 2.0 - 14.0;
        g.setBrush (Qt::NoBrush);
        g.setPen (QPen (line, 1.0));
        g.drawEllipse (sc, sr, sr);
        g.setPen (QPen (line, 1.0, Qt::DotLine));
        g.drawLine (QPointF (sc.x() - sr, sc.y()), QPointF (sc.x() + sr, sc.y()));
        const auto sh = sr * 0.14;
        g.setPen (QPen (theme.color (QStringLiteral ("windowText")), 1.5));
        g.drawEllipse (sc, sh, sh);
        g.drawLine (QPointF (sc.x() + sh * 0.92, sc.y() - sh * 0.35), QPointF (sc.x() + sh * 1.45, sc.y()));
        g.drawLine (QPointF (sc.x() + sh * 0.92, sc.y() + sh * 0.35), QPointF (sc.x() + sh * 1.45, sc.y()));
        g.setPen (text);
        g.drawText (QRectF (side.left(), side.top() - 2, side.width(), 14), Qt::AlignCenter, tr ("above"));
        g.drawText (QRectF (side.left(), side.bottom() - 12, side.width(), 14), Qt::AlignCenter, tr ("below"));
        const auto facing = std::abs (wrap (azimuth)) <= 90.0 ? 1.0 : -1.0;
        const auto p = QPointF (sc.x() + facing * sr * 0.8 * std::cos (radians (elevation)), sc.y() - sr * 0.8 * std::sin (radians (elevation)));
        g.setPen (QPen (focused ? accent : sound.darker (150), 2.0));
        g.setBrush (sound);
        g.drawEllipse (p, 7.0, 7.0);
        g.setPen (text);
        g.drawText (QRectF (side.left(), side.bottom() + 0, side.width(), 14), Qt::AlignCenter,
                    tr ("%1°").arg (elevation, 0, 'f', 0));
    }
}

void BinauralField::dragTo (QPointF p, bool topView)
{
    if (topView)
    {
        const auto top = topArea();
        const auto c = top.center();
        const auto R = top.width() / 2.0 - 14.0;
        const auto d = p - c;
        azimuth = std::round (wrap (std::atan2 (d.x(), -d.y()) * 180.0 / M_PI));
        if (withDistance)
            distance = distanceFor (std::hypot (d.x(), d.y()) / R);
    }
    else
    {
        const auto c = sideArea().center();
        const auto d = p - c;
        elevation = std::round (std::clamp (std::atan2 (-d.y(), std::abs (d.x())) * 180.0 / M_PI, -40.0, 90.0));
    }
    changed();
}

void BinauralField::mousePressEvent (QMouseEvent* e)
{
    setFocus (Qt::MouseFocusReason);
    const auto p = e->position();
    dragging = topArea().contains (p) ? 1 : sideArea().contains (p) ? 2 : 0;
    if (dragging != 0)
        dragTo (p, dragging == 1);
}

void BinauralField::mouseMoveEvent (QMouseEvent* e)
{
    if (dragging != 0 && (e->buttons() & Qt::LeftButton))
        dragTo (e->position(), dragging == 1);
}

void BinauralField::wheelEvent (QWheelEvent* e)
{
    const auto steps = e->angleDelta().y() / 120.0;
    if (stereo)
        spread = std::clamp (spread + steps * 5.0, 0.0, 180.0);
    else if (withDistance)
        distance = distanceFor (radiusFor (distance) + steps * 0.03);
    changed();
}

void BinauralField::keyPressEvent (QKeyEvent* e)
{
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    switch (e->key())
    {
        case Qt::Key_Left:
        case Qt::Key_Right:
        {
            const auto sign = e->key() == Qt::Key_Right ? 1.0 : -1.0;
            if (shift && stereo)
                spread = std::clamp (spread + sign * 5.0, 0.0, 180.0);
            else
                azimuth = wrap (azimuth + sign * 5.0);
            break;
        }
        case Qt::Key_Up:   if (withDistance) distance = distanceFor (radiusFor (distance) - 0.03); break;
        case Qt::Key_Down: if (withDistance) distance = distanceFor (radiusFor (distance) + 0.03); break;
        case Qt::Key_PageUp:   elevation = std::clamp (elevation + 5.0, -40.0, 90.0); break;
        case Qt::Key_PageDown: elevation = std::clamp (elevation - 5.0, -40.0, 90.0); break;
        default:
            QWidget::keyPressEvent (e);
            return;
    }
    changed();
}

void BinauralField::changed()
{
    distance = std::round (distance * 100.0) / 100.0;
    update();
    Q_EMIT moved (azimuth, elevation, distance, spread);
}

//==============================================================================
BinauralDialog::BinauralDialog (const binaural::Settings& initial, bool stereo, bool wholeFile, const QStringList& hrtfFiles,
                                std::function<void (const binaural::Settings*)> p, QWidget* parent)
    : QDialog (parent), preview (std::move (p))
{
    setWindowTitle (tr ("Binaural"));

    field = new BinauralField (stereo, this);
    const auto box = [this] (double lo, double hi, double step, int decimals, const QString& suffix)
    {
        auto* b = new QDoubleSpinBox (this);
        b->setRange (lo, hi);
        b->setSingleStep (step);
        b->setDecimals (decimals);
        b->setSuffix (suffix);
        b->setAccelerated (true);
        return b;
    };
    azimuthBox = box (-180.0, 180.0, 5.0, 0, tr ("°"));
    azimuthBox->setWrapping (true);
    azimuthBox->setToolTip (tr ("0° is in front, 90° right, -90° left, 180° behind"));
    elevationBox = box (-40.0, 90.0, 5.0, 0, tr ("°"));
    elevationBox->setToolTip (tr ("0° is at ear height, 90° straight above"));
    if (stereo)
    {
        widthBox = box (0.0, 180.0, 5.0, 0, tr ("°"));
        widthBox->setToolTip (tr ("How far apart the left and right channels are; 60° is like a pair of speakers"));
    }
    distanceBox = box (nearest, furthest, 0.1, 2, tr (" m"));
    distanceBox->setToolTip (tr ("Further away is quieter and a little duller"));
    roomBox = new QSpinBox (this);
    roomBox->setRange (0, 100);
    roomBox->setSuffix (tr (" %"));
    roomBox->setToolTip (tr ("Room sound helps the sound feel outside your head; 0% is dry"));
    roomSecondsBox = box (0.1, 4.0, 0.1, 1, tr (" s"));
    roomSecondsBox->setToolTip (tr ("How long the room rings (RT60)"));
    gainBox = box (-24.0, 24.0, 0.5, 1, tr (" dB"));

    hrtfBox = new QComboBox (this);
    for (const auto& f : hrtfFiles)
        hrtfBox->addItem (QFileInfo (f).completeBaseName(), f);
    if (! initial.hrtfPath.empty() && hrtfBox->findData (QString::fromStdString (initial.hrtfPath)) < 0)
        hrtfBox->addItem (QFileInfo (QString::fromStdString (initial.hrtfPath)).completeBaseName(), QString::fromStdString (initial.hrtfPath));
    hrtfBox->setCurrentIndex (std::max (0, hrtfBox->findData (QString::fromStdString (initial.hrtfPath))));
    hrtfBox->setToolTip (tr ("The ears to listen with: a measured HRTF set (SOFA file). Your own sets go in ~/.local/share/phinyx/hrtf"));
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

    previewBox = new QCheckBox (tr ("&Preview (headphones)"), this);
    previewBox->setToolTip (tr ("Plays the result in a loop and follows your changes"));

    auto* form = new QFormLayout;
    form->addRow (tr ("&Azimuth:"), azimuthBox);
    form->addRow (tr ("&Elevation:"), elevationBox);
    if (widthBox != nullptr)
        form->addRow (tr ("&Width:"), widthBox);
    form->addRow (tr ("&Distance:"), distanceBox);
    form->addRow (tr ("&Room:"), roomBox);
    form->addRow (tr ("Room de&cay:"), roomSecondsBox);
    form->addRow (tr ("&Output:"), gainBox);
    auto* hrtfLabel = new QLabel (tr ("&HRTF set:"), this);
    hrtfLabel->setBuddy (hrtfBox);
    form->addRow (hrtfLabel, hrtfRow);

    auto* about = new QLabel (this);
    about->setWordWrap (true);
    about->setText ((stereo ? tr ("Places this stereo sound around your head for headphones: its two channels become two sources.")
                            : tr ("Places this mono sound around your head for headphones."))
                    + QLatin1Char (' ')
                    + (wholeFile ? tr ("It applies to the whole file") : tr ("It applies to the selection; its tail rings on after it"))
                    + tr (", and the result is stereo."));

    auto* columns = new QHBoxLayout;
    columns->addWidget (field, 1);
    columns->addLayout (form);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout (this);
    layout->addWidget (about);
    layout->addLayout (columns, 1);
    layout->addWidget (previewBox);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    azimuthBox->setValue (initial.azimuth);
    elevationBox->setValue (initial.elevation);
    if (widthBox != nullptr)
        widthBox->setValue (initial.width);
    distanceBox->setValue (initial.distance);
    roomBox->setValue ((int) std::lround (initial.room * 100.0));
    roomSecondsBox->setValue (initial.roomSeconds);
    gainBox->setValue (initial.gainDb);
    field->set (settings());

    connect (field, &BinauralField::moved, this, &BinauralDialog::fromField);
    for (auto* b : { azimuthBox, elevationBox, widthBox, distanceBox, roomSecondsBox, gainBox })
        if (b != nullptr)
            connect (b, &QDoubleSpinBox::valueChanged, this, &BinauralDialog::changed);
    connect (roomBox, &QSpinBox::valueChanged, this, &BinauralDialog::changed);
    connect (hrtfBox, &QComboBox::currentIndexChanged, this, &BinauralDialog::changed);

    // Rendering follows the changes after a short pause, so dragging stays smooth.
    previewTimer.setSingleShot (true);
    previewTimer.setInterval (250);
    connect (&previewTimer, &QTimer::timeout, this, [this]
    {
        if (previewBox->isChecked() && preview)
        {
            const auto s = settings();
            preview (&s);
        }
    });
    connect (previewBox, &QCheckBox::toggled, this, [this] (bool on)
    {
        if (on)
            previewTimer.start (0);
        else if (preview)
            preview (nullptr);
    });

    field->setFocus();
}

BinauralDialog::~BinauralDialog() = default;

binaural::Settings BinauralDialog::settings() const
{
    binaural::Settings s;
    s.azimuth = azimuthBox->value();
    s.elevation = elevationBox->value();
    s.width = widthBox != nullptr ? widthBox->value() : 60.0;
    s.distance = distanceBox->value();
    s.room = roomBox->value() / 100.0;
    s.roomSeconds = roomSecondsBox->value();
    s.gainDb = gainBox->value();
    s.hrtfPath = hrtfBox->currentData().toString().toStdString();
    return s;
}

void BinauralDialog::fromField (double azimuth, double elevation, double distance, double width)
{
    updating = true;
    azimuthBox->setValue (azimuth);
    elevationBox->setValue (elevation);
    distanceBox->setValue (distance);
    if (widthBox != nullptr)
        widthBox->setValue (width);
    updating = false;
    changed();
}

void BinauralDialog::changed()
{
    if (! updating)
        field->set (settings());
    if (previewBox->isChecked())
        previewTimer.start();
}

void BinauralDialog::done (int result)
{
    previewTimer.stop();
    if (previewBox->isChecked() && preview)
        preview (nullptr);
    QDialog::done (result);
}
