// SPDX-License-Identifier: AGPL-3.0-only
#include "MixerSpatial.h"

#include "MixerWindow.h"
#include "Settings.h"
#include "SpeakerArrays.h"
#include "Theme.h"
#include "../core/Dsp.h"
#include "../engine/AudioEngine.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    // Distance on the room view: the speakers' ring (1) at half the radius, then by its logarithm out to maxDistance.
    constexpr double ring = 0.5;

    double fraction (double distance)
    {
        const auto d = std::clamp (distance, 0.0, (double) mixer::spatial::maxDistance);
        return d <= 1.0 ? ring * d : ring + (1.0 - ring) * std::log (d) / std::log ((double) mixer::spatial::maxDistance);
    }

    double distanceAt (double f)
    {
        f = std::clamp (f, 0.0, 1.0);
        return f <= ring ? f / ring : std::pow ((double) mixer::spatial::maxDistance, (f - ring) / (1.0 - ring));
    }

    // The settings the decoder is made from: the studio's, so in the app's settings, not the project.
    QString key (const char* name) { return QStringLiteral ("mixer/spatial/") + QLatin1String (name); }

    std::vector<ambi::Speaker> speakersNamed (const QString& name)
    {
        if (name.startsWith (QStringLiteral ("Custom: ")))
            return speakerarrays::get (name.mid (8));
        return name.isEmpty() ? std::vector<ambi::Speaker> {} : ambi::layout (name.toStdString());
    }

    QString placeText (const mixer::Panner3d& p)
    {
        return QObject::tr ("azimuth %1°, elevation %2°, spread %3°, distance %4")
            .arg (std::lround (p.azimuth.load())).arg (std::lround (p.elevation.load())).arg (std::lround (p.spread.load()))
            .arg (p.distance.load(), 0, 'f', 1);
    }
}

//==============================================================================
RoomView::RoomView (MixerWindow& w, QWidget* parent) : QWidget (parent), window (w)
{
    setMinimumSize (180, 180);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMouseTracking (false);
    setFocusPolicy (Qt::NoFocus);
}

std::vector<RoomView::Source> RoomView::sources() const
{
    std::vector<Source> list;
    auto& e = window.engine;
    bool anyToBus = false;
    for (int i = 0; i < e.numTracks(); ++i)
    {
        auto& s = e.strip (i);
        if (s.toBus.load())
        {
            anyToBus = true;
            continue;   // heard where the Pre-Master is
        }
        const auto& info = window.info[(size_t) i];
        list.push_back ({ i, &s.placed, QString::number (i + 1), info.name, info.colour });
    }
    if (anyToBus)
        list.push_back ({ preMasterKey, &e.busStrip().placed, QStringLiteral ("PM"), MixerWindow::tr ("Pre-Master"),
                          Theme::current().color (QStringLiteral ("highlight")) });
    for (int r = 0; r < mixer::numReturns; ++r)
        list.push_back ({ returnKey - r, &e.returnStrip (r).placed, QString (QChar ('A' + r)),
                          MixerWindow::returnName (r), QColor (0x80, 0x86, 0x90) });
    return list;
}

QPointF RoomView::centre() const { return { width() / 2.0, height() / 2.0 }; }
double RoomView::radius() const { return std::max (10.0, std::min (width(), height()) / 2.0 - 16.0); }

QPointF RoomView::where (double azimuth, double distance) const
{
    // From above, ahead at the top: azimuth 90 is to the right.
    const auto a = azimuth * pi / 180.0;
    const auto r = fraction (distance) * radius();
    return centre() + QPointF (r * std::sin (a), -r * std::cos (a));
}

int RoomView::sourceAt (QPointF at) const
{
    const auto list = sources();
    int best = -1;
    double nearest = 14.0;
    for (int k = (int) list.size() - 1; k >= 0; --k)
    {
        const auto& p = *list[(size_t) k].panner;
        const auto d = QLineF (at, where (p.azimuth.load(), p.distance.load())).length();
        if (d < nearest)
        {
            nearest = d;
            best = k;
        }
    }
    return best;
}

void RoomView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto pal = palette();
    const auto c = centre();
    const auto r = radius();
    const auto text = pal.color (QPalette::WindowText);
    auto dim = text;
    dim.setAlphaF (0.35f);

    // The room: out to the farthest distance, the speakers' ring at 1, and the cross.
    g.setPen (QPen (dim, 1.0));
    g.setBrush (pal.color (QPalette::Base));
    g.drawEllipse (c, r, r);
    g.setBrush (Qt::NoBrush);
    g.setPen (QPen (dim, 1.0, Qt::DashLine));
    g.drawEllipse (c, r * ring, r * ring);
    g.drawEllipse (c, r * fraction (3.0), r * fraction (3.0));
    g.setPen (QPen (dim, 1.0, Qt::DotLine));
    g.drawLine (QPointF (c.x() - r, c.y()), QPointF (c.x() + r, c.y()));
    g.drawLine (QPointF (c.x(), c.y() - r), QPointF (c.x(), c.y() + r));
    auto small = font();
    small.setPointSizeF (std::max (6.5, small.pointSizeF() * 0.8));
    g.setFont (small);
    g.setPen (dim);
    g.drawText (QRectF (c.x() - 40, c.y() - r - 15, 80, 14), Qt::AlignCenter, MixerWindow::tr ("Front"));

    // The speakers, at their direction on the ring (raised ones nearer the middle, as from above), by output.
    const auto first = appSettings().value (key ("firstOutput"), 3).toInt();
    const auto& speakers = window.spatialSpeakers;
    for (size_t l = 0; l < speakers.size(); ++l)
    {
        const auto& s = speakers[l];
        if (s.lfe)
            continue;
        const auto a = s.azimuth * pi / 180.0;
        const auto sr = r * ring * std::cos (std::clamp (s.elevation, -90.0, 90.0) * pi / 180.0);
        const QPointF at = c + QPointF (sr * std::sin (a), -sr * std::cos (a));
        g.setPen (QPen (text, 1.0));
        g.setBrush (s.elevation > 5.0 ? QBrush (Qt::NoBrush) : QBrush (pal.color (QPalette::Mid)));
        g.drawRect (QRectF (at.x() - 5, at.y() - 5, 10, 10));
        g.setPen (dim);
        g.drawText (QRectF (at.x() - 15, at.y() + 5, 30, 12), Qt::AlignCenter, QString::number (first + (int) l));
    }

    // The listener, facing the front.
    g.setPen (QPen (text, 1.2));
    g.setBrush (Qt::NoBrush);
    g.drawEllipse (c, 6.0, 6.0);
    g.drawLine (c + QPointF (-3, -5), c + QPointF (0, -10));
    g.drawLine (c + QPointF (3, -5), c + QPointF (0, -10));

    // The sources: the two sides of each (spread apart), then its puck, with its height if it is raised.
    const auto list = sources();
    for (const auto& src : list)
    {
        const auto& p = *src.panner;
        const double az = p.azimuth.load(), d = p.distance.load(), half = p.spread.load() / 2.0;
        auto faint = src.colour;
        faint.setAlphaF (0.55f);
        g.setPen (QPen (faint, 2.0));
        const auto rr = fraction (d) * r;
        if (half > 0.5 && rr > 1.0)
        {
            const QRectF box (c.x() - rr, c.y() - rr, 2 * rr, 2 * rr);
            // Qt: counter-clockwise from 3 o'clock; azimuth: clockwise from 12.
            g.drawArc (box, (int) std::lround ((90.0 - az - half) * 16.0), (int) std::lround (2.0 * half * 16.0));
            g.setBrush (faint);
            g.drawEllipse (where (az - half, d), 3.0, 3.0);
            g.drawEllipse (where (az + half, d), 3.0, 3.0);
        }
        const auto at = where (az, d);
        const bool chosen = src.key >= 0 && src.key == window.selected;
        g.setPen (QPen (chosen ? pal.color (QPalette::HighlightedText) : src.colour.darker (150), chosen ? 2.0 : 1.0));
        g.setBrush (src.colour);
        g.drawEllipse (at, 9.0, 9.0);
        g.setPen (Qt::white);
        g.drawText (QRectF (at.x() - 9, at.y() - 9, 18, 18), Qt::AlignCenter, src.label);
        if (std::abs (p.elevation.load()) >= 1.0f)
        {
            g.setPen (text);
            g.drawText (QRectF (at.x() + 9, at.y() - 16, 40, 12), Qt::AlignLeft,
                        QStringLiteral ("%1%2°").arg (p.elevation.load() > 0 ? QStringLiteral ("↑") : QStringLiteral ("↓"))
                            .arg (std::lround (std::abs (p.elevation.load()))));
        }
    }

    if (std::abs (window.sceneYaw) >= 0.5)
    {
        g.setPen (dim);
        g.drawText (QRectF (4, height() - 16, width() - 8, 14), Qt::AlignLeft | Qt::AlignVCenter,
                    MixerWindow::tr ("The scene turned %1°").arg (std::lround (window.sceneYaw)));
    }
}

void RoomView::mousePressEvent (QMouseEvent* e)
{
    const auto k = sourceAt (e->position());
    if (k < 0 || e->button() != Qt::LeftButton)
        return QWidget::mousePressEvent (e);
    const auto src = sources()[(size_t) k];
    dragging = src.key;
    dragged = false;
    if (src.key >= 0 && src.key != window.selected)
        window.select (src.key);
}

void RoomView::mouseMoveEvent (QMouseEvent* e)
{
    if (dragging == -100)
        return;
    for (const auto& src : sources())
        if (src.key == dragging)
        {
            const auto v = e->position() - centre();
            if (std::hypot (v.x(), v.y()) < 1.0)
                return;
            auto& p = *src.panner;
            p.azimuth = (float) (std::atan2 (v.x(), -v.y()) * 180.0 / pi);
            if (! (e->modifiers() & Qt::ShiftModifier))
                p.distance = (float) distanceAt (std::hypot (v.x(), v.y()) / radius());
            dragged = true;
            moved();
            return;
        }
}

void RoomView::mouseReleaseEvent (QMouseEvent* e)
{
    if (dragged)
        window.changed();
    dragging = -100;
    dragged = false;
    QWidget::mouseReleaseEvent (e);
}

void RoomView::mouseDoubleClickEvent (QMouseEvent* e)
{
    const auto k = sourceAt (e->position());
    if (k < 0)
        return;
    const auto src = sources()[(size_t) k];
    src.panner->set ({ 0.0f, 0.0f, src.key <= returnKey ? mixer::Panner3d::returnSpread : mixer::Panner3d::defaultSpread, 1.0f });
    moved();
    window.changed();
}

void RoomView::wheelEvent (QWheelEvent* e)
{
    const auto k = sourceAt (e->position());
    if (k < 0)
        return QWidget::wheelEvent (e);
    // Alt (and on some desktops Shift) turns a wheel sideways.
    const auto delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    const auto steps = delta / 120.0;
    auto& p = *sources()[(size_t) k].panner;
    if (e->modifiers() & Qt::ShiftModifier)
        p.spread = (float) std::clamp (p.spread.load() + steps * 10.0, 0.0, 360.0);
    else
        p.elevation = (float) std::clamp (p.elevation.load() + steps * 5.0, -90.0, 90.0);
    moved();
    window.changed();
    e->accept();
}

bool RoomView::event (QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        const auto* help = static_cast<QHelpEvent*> (e);
        const auto k = sourceAt (help->pos());
        if (k >= 0)
        {
            const auto src = sources()[(size_t) k];
            QToolTip::showText (help->globalPos(), QStringLiteral ("%1: %2").arg (src.name, placeText (*src.panner)), this);
        }
        else
            QToolTip::showText (help->globalPos(), MixerWindow::tr ("The room from above, the front at the top. Drag a source to place it "
                                                                    "(Shift keeps its distance); the wheel over it raises it, Shift+wheel widens it; "
                                                                    "double-click puts it back ahead. The inner ring is distance 1, where the speakers are"),
                                this);
        return true;
    }
    return QWidget::event (e);
}

void RoomView::moved()
{
    window.spatialMoved();
}

//==============================================================================
SpeakerMeter::SpeakerMeter (MixerWindow& w, QWidget* parent) : QWidget (parent), window (w)
{
    setMinimumHeight (70);
    setToolTip (MixerWindow::tr ("The level of each speaker's feed, after the master fader: the speakers of the array in their order, "
                                 "the subwoofers (LFE) with the LFE sends and the surround files' LFE channels"));
    auto* timer = new QTimer (this);
    connect (timer, &QTimer::timeout, this, [this] { take(); });
    timer->start (40);
}

void SpeakerMeter::take()
{
    const auto peaks = window.engine.takeSpeakerPeaks();
    if (peaks.size() != level.size())
    {
        level.assign (peaks.size(), -96.0f);
        hold.assign (peaks.size(), -96.0f);
        held.assign (peaks.size(), 0);
    }
    for (size_t l = 0; l < peaks.size(); ++l)
    {
        const auto db = peaks[l] > 1.0e-5f ? 20.0f * std::log10 (peaks[l]) : -96.0f;
        level[l] = std::max (db, level[l] - 0.8f);   // 20 dB a second at 25 looks a second
        if (db >= hold[l])
        {
            hold[l] = db;
            held[l] = 0;
        }
        else if (++held[l] > 50)   // two seconds
        {
            hold[l] = level[l];
            held[l] = 0;
        }
    }
    if (isVisible())
        update();
}

void SpeakerMeter::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto& speakers = window.spatialSpeakers;
    const auto count = std::min (speakers.size(), level.size());
    if (count == 0)
    {
        g.setPen (palette().color (QPalette::PlaceholderText));
        g.drawText (rect(), Qt::AlignCenter, window.spatialMode() ? MixerWindow::tr ("No speakers: pick an array above") : MixerWindow::tr ("Spatial mixing is off"));
        return;
    }
    const auto labelHeight = g.fontMetrics().height();
    const auto gap = 3.0;
    const auto barWidth = std::max (4.0, ((double) width() - gap * (double) (count + 1)) / (double) count);
    const auto top = 2.0, bottom = (double) height() - labelHeight - 2.0;
    const auto y = [&] (float db) { return bottom - (bottom - top) * (double) std::clamp ((db + 60.0f) / 66.0f, 0.0f, 1.0f); };
    g.setFont (font());
    for (size_t l = 0; l < count; ++l)
    {
        const QRectF bar (gap + (double) l * (barWidth + gap), top, barWidth, bottom - top);
        g.fillRect (bar, palette().color (QPalette::Base));
        const auto db = level[l];
        const auto colour = db > -0.1f ? QColor (220, 60, 50) : db > -6.0f ? QColor (230, 190, 60) : QColor (80, 190, 110);
        g.fillRect (QRectF (bar.left(), y (db), bar.width(), bar.bottom() - y (db)), speakers[l].lfe ? colour.darker (130) : colour);
        g.setPen (palette().color (QPalette::WindowText));
        g.drawLine (QPointF (bar.left(), y (hold[l])), QPointF (bar.right(), y (hold[l])));
        g.setPen (palette().color (speakers[l].lfe ? QPalette::PlaceholderText : QPalette::WindowText));
        g.drawText (QRectF (bar.left() - gap, bottom + 1.0, bar.width() + 2 * gap, labelHeight), Qt::AlignCenter,
                    QString::fromStdString (speakers[l].name.empty() ? std::to_string (l + 1) : speakers[l].name));
    }
}

//==============================================================================
SpatialDialog::SpatialDialog (MixerWindow& w, QWidget* parent) : QDialog (parent), window (w)
{
    setWindowTitle (MixerWindow::tr ("Spatial Mixing"));
    setAttribute (Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout (this);
    onBox = new QCheckBox (MixerWindow::tr ("Spatial mixing in this project"), this);
    onBox->setToolTip (MixerWindow::tr ("Each track, the Pre-Master and the returns are placed around you by a 3D panner (in place of pan and width) "
                                        "in one 3rd-order ambisonic scene, heard on headphones and on the speakers at once"));
    layout->addWidget (onBox);

    auto* form = new QFormLayout;
    phonesBox = new QComboBox (this);
    phonesBox->addItem (MixerWindow::tr ("Binaural (HRTF)"), QStringLiteral ("binaural"));
    phonesBox->addItem (MixerWindow::tr ("Stereo decode"), QStringLiteral ("stereo"));
    phonesBox->addItem (MixerWindow::tr ("Off (speakers only)"), QStringLiteral ("off"));
    phonesBox->setToolTip (MixerWindow::tr ("What the stereo out (outputs 1 and 2, the mix down) carries: the scene through an HRTF set for headphones, "
                                            "a plain stereo decode for two speakers, or nothing"));
    form->addRow (MixerWindow::tr ("Headphones:"), phonesBox);

    hrtfBox = new QComboBox (this);
    auto* hrtfOther = new QPushButton (MixerWindow::tr ("Other..."), this);
    auto* hrtfRow = new QHBoxLayout;
    hrtfRow->addWidget (hrtfBox, 1);
    hrtfRow->addWidget (hrtfOther);
    hrtfBox->setToolTip (MixerWindow::tr ("The HRTF set (.sofa file) the binaural decode uses"));
    form->addRow (MixerWindow::tr ("HRTF set:"), hrtfRow);

    speakersBox = new QComboBox (this);
    speakersBox->setToolTip (MixerWindow::tr ("Your speaker array (make it in Spatial > Speaker Arrays) or a standard layout, fed from the same scene"));
    auto* arrays = new QPushButton (MixerWindow::tr ("Speaker Arrays..."), this);
    auto* speakerRow = new QHBoxLayout;
    speakerRow->addWidget (speakersBox, 1);
    speakerRow->addWidget (arrays);
    form->addRow (MixerWindow::tr ("Speakers:"), speakerRow);

    firstBox = new QSpinBox (this);
    firstBox->setRange (1, 64);
    firstBox->setToolTip (MixerWindow::tr ("The device output the first speaker is on; the rest follow in the array's order. "
                                           "From 3 on, the headphones keep outputs 1 and 2 to themselves"));
    form->addRow (MixerWindow::tr ("First speaker on output:"), firstBox);

    yawBox = new QSpinBox (this);
    yawBox->setRange (-180, 180);
    yawBox->setSuffix (QStringLiteral ("°"));
    yawBox->setToolTip (MixerWindow::tr ("The whole scene turned to the right (negative: to the left), before it is decoded"));
    form->addRow (MixerWindow::tr ("Turn the scene:"), yawBox);

    renderBox = new QComboBox (this);
    renderBox->addItem (MixerWindow::tr ("The headphone mix (stereo)"), (int) mixer::Engine::SpatialRender::headphones);
    renderBox->addItem (MixerWindow::tr ("The speakers, a channel each (5.1, 7.1.4...)"), (int) mixer::Engine::SpatialRender::speakers);
    renderBox->addItem (MixerWindow::tr ("The scene as AmbiX (3rd order, 16 channels)"), (int) mixer::Engine::SpatialRender::ambix);
    renderBox->setToolTip (MixerWindow::tr ("What Mix Down to Editor renders in Spatial mode: the headphone decode, one channel per speaker "
                                            "in the array's order (a 5.1.4 or 7.1.4 file, with its LFE), or the ambisonic scene itself "
                                            "before it is decoded, for another decoder or a 360° video"));
    form->addRow (MixerWindow::tr ("Mix Down renders:"), renderBox);
    layout->addLayout (form);

    room = new RoomView (window, this);
    room->setMinimumSize (320, 320);
    layout->addWidget (room, 1);
    meter = new SpeakerMeter (window, this);
    layout->addWidget (meter);
    status = new QLabel (this);
    status->setWordWrap (true);
    layout->addWidget (status);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, this);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    connect (onBox, &QCheckBox::toggled, this, [this] (bool on) { if (! updating) window.setSpatialMode (on); });
    for (auto* box : { phonesBox, hrtfBox, speakersBox })
        connect (box, &QComboBox::currentIndexChanged, this, [this] { save(); });
    connect (firstBox, &QSpinBox::valueChanged, this, [this] { save(); });
    connect (renderBox, &QComboBox::currentIndexChanged, this, [this]
    {
        if (updating)
            return;
        window.engine.spatialRender = (mixer::Engine::SpatialRender) renderBox->currentData().toInt();
        window.changed();
    });
    connect (yawBox, &QSpinBox::valueChanged, this, [this] (int v)
    {
        if (updating)
            return;
        window.sceneYaw = v;
        window.changed();
        save();
    });
    connect (hrtfOther, &QPushButton::clicked, this, [this]
    {
        const auto file = QFileDialog::getOpenFileName (this, MixerWindow::tr ("HRTF Set"), QFileInfo (hrtfBox->currentData().toString()).absolutePath(),
                                                        MixerWindow::tr ("SOFA files (*.sofa)"));
        if (file.isEmpty())
            return;
        if (hrtfBox->findData (file) < 0)
            hrtfBox->addItem (QFileInfo (file).fileName(), file);
        hrtfBox->setCurrentIndex (hrtfBox->findData (file));
    });
    connect (arrays, &QPushButton::clicked, this, [this]
    {
        const auto current = speakersBox->currentData().toString();
        SpeakerArraysDialog dialog (current.startsWith (QStringLiteral ("Custom: ")) ? current.mid (8) : QString(), this);
        dialog.exec();
        fillSpeakers();
        if (! dialog.chosen().isEmpty())
            speakersBox->setCurrentIndex (std::max (0, speakersBox->findData (QStringLiteral ("Custom: ") + dialog.chosen())));
        save();
    });
    refresh();
}

void SpatialDialog::fillSpeakers()
{
    const QSignalBlocker block (speakersBox);
    const auto keep = speakersBox->count() > 0 ? speakersBox->currentData().toString()
                                               : appSettings().value (key ("speakers")).toString();
    speakersBox->clear();
    speakersBox->addItem (MixerWindow::tr ("None"), QString());
    for (const auto& name : speakerarrays::names())
        speakersBox->addItem (MixerWindow::tr ("Custom: %1 (%2 speakers)").arg (name).arg (speakerarrays::get (name).size()), QStringLiteral ("Custom: ") + name);
    for (const auto& name : ambi::layoutNames())
        if (name != "Stereo")
            speakersBox->addItem (QString::fromStdString (name), QString::fromStdString (name));
    speakersBox->setCurrentIndex (std::max (0, speakersBox->findData (keep)));
}

void SpatialDialog::refresh()
{
    updating = true;
    const QSignalBlocker a (phonesBox), b (hrtfBox), c (firstBox), d (yawBox), e (renderBox);
    renderBox->setCurrentIndex (std::max (0, renderBox->findData ((int) window.engine.spatialRender.load())));
    auto s = appSettings();
    onBox->setChecked (window.spatialMode());
    phonesBox->setCurrentIndex (std::max (0, phonesBox->findData (s.value (key ("headphones"), QStringLiteral ("binaural")).toString())));
    hrtfBox->clear();
    for (const auto& file : spatialHrtfFiles())
        hrtfBox->addItem (QFileInfo (file).fileName(), file);
    const auto hrtf = s.value (key ("hrtf")).toString();
    if (! hrtf.isEmpty() && hrtfBox->findData (hrtf) < 0 && QFileInfo::exists (hrtf))
        hrtfBox->addItem (QFileInfo (hrtf).fileName(), hrtf);
    hrtfBox->setCurrentIndex (std::max (0, hrtfBox->findData (hrtf)));
    hrtfBox->setEnabled (phonesBox->currentData().toString() == QStringLiteral ("binaural"));
    fillSpeakers();
    firstBox->setValue (s.value (key ("firstOutput"), 3).toInt());
    firstBox->setEnabled (speakersBox->currentIndex() > 0);
    yawBox->setValue ((int) std::lround (window.sceneYaw));
    status->setText (window.spatialStatus);
    room->update();
    updating = false;
}

void SpatialDialog::save()
{
    if (updating)
        return;
    auto s = appSettings();
    s.setValue (key ("headphones"), phonesBox->currentData().toString());
    s.setValue (key ("hrtf"), hrtfBox->currentData().toString());
    s.setValue (key ("speakers"), speakersBox->currentData().toString());
    s.setValue (key ("firstOutput"), firstBox->value());
    s.sync();
    window.applySpatial();
    refresh();
}

//==============================================================================
bool MixerWindow::spatialMode() const { return engine.spatialOn.load(); }

void MixerWindow::showSpatialDialog()
{
    if (spatialDialog == nullptr)
        spatialDialog = new SpatialDialog (*this, this);
    spatialDialog->refresh();
    spatialDialog->show();
    spatialDialog->raise();
    spatialDialog->activateWindow();
}

void MixerWindow::applySpatial()
{
    auto settings = appSettings();
    const auto phones = settings.value (key ("headphones"), QStringLiteral ("binaural")).toString();
    const auto first = std::max (0, settings.value (key ("firstOutput"), 3).toInt() - 1);
    const auto speakersName = settings.value (key ("speakers")).toString();
    spatialSpeakers = speakersNamed (speakersName);
    if (! spatialMode())
    {
        engine.setDecoder (nullptr);
        audio.setSpeakerFeed ({}, 0);
        spatialStatus = tr ("Spatial mixing is off: the mix is stereo, with pan and width.");
        spatialMoved();
        return;
    }

    mixer::spatial::Decoder::Setup setup;
    setup.headphones = phones != QStringLiteral ("off");
    setup.yaw = sceneYaw;
    QStringList said;
    if (phones == QStringLiteral ("binaural"))
    {
        auto file = settings.value (key ("hrtf")).toString();
        const auto found = spatialHrtfFiles();
        if (! QFileInfo::exists (file))
            file = found.value (0);
        if (file.isEmpty())
            said << tr ("No HRTF set (.sofa) was found, so the headphones hear a stereo decode.");
        else
        {
            QApplication::setOverrideCursor (Qt::WaitCursor);
            std::string error;
            setup.ears = ambi::binauralFilters (file.toStdString(), mixer::spatial::order, rate(), error);
            QApplication::restoreOverrideCursor();
            said << (setup.ears != nullptr ? tr ("Headphones: binaural through %1.").arg (QFileInfo (file).fileName())
                                           : tr ("%1 could not be read (%2), so the headphones hear a stereo decode.")
                                                 .arg (QFileInfo (file).fileName(), QString::fromStdString (error)));
        }
    }
    else
        said << (setup.headphones ? tr ("Headphones: a stereo decode.") : tr ("Headphones: off."));
    if (! spatialSpeakers.empty())
    {
        setup.speakers = ambi::decoder (mixer::spatial::order, spatialSpeakers);
        setup.firstOutput = first;
        for (size_t l = 0; l < spatialSpeakers.size(); ++l)
            if (spatialSpeakers[l].lfe)
                setup.lfe.push_back ((int) l);
    }
    else if (! speakersName.isEmpty())
        said << tr ("The speaker array %1 is empty or gone.").arg (speakersName);

    auto decoder = std::make_shared<mixer::spatial::Decoder> (std::move (setup));
    const auto count = decoder->speakerCount();
    const bool phonesOn = decoder->setup().headphones;
    engine.setDecoder (std::move (decoder));
    if (count > 0)
    {
        audio.setSpeakerFeed ([e = &engine] (float* const* outs, int numOuts, int frames) { e->takeSpeakers (outs, numOuts, frames); }, first + count);
        const auto open = audio.outputChannels();
        said << (open >= first + count ? tr ("%1 speaker channels on outputs %2 to %3.").arg (count).arg (first + 1).arg (first + count)
                 : open == 0 ? tr ("%1 speaker channels on outputs %2 to %3, but no audio device is open.").arg (count).arg (first + 1).arg (first + count)
                             : tr ("%1 speaker channels on outputs %2 to %3, but the device has only %4 outputs open: pick a device "
                                   "with more in Options > Audio Settings.").arg (count).arg (first + 1).arg (first + count).arg (open));
        if (phonesOn && first < 2)
            said << tr ("The headphones share outputs 1 and 2 with the first speakers.");
    }
    else
    {
        audio.setSpeakerFeed ({}, 0);
        said << tr ("No speakers.");
    }
    spatialStatus = said.join (QLatin1Char (' '));
    spatialMoved();
}

QJsonObject MixerWindow::spatialJson() const
{
    static const char* renders[] { "headphones", "speakers", "ambix" };
    return { { "on", spatialMode() }, { "yaw", sceneYaw }, { "mixDown", renders[(int) engine.spatialRender.load()] } };
}

std::string MixerWindow::mixdownLayout() const
{
    if (! spatialMode() || engine.decoder() == nullptr)
        return {};
    switch (engine.spatialRender.load())
    {
        case mixer::Engine::SpatialRender::ambix: return "Ambisonics (3rd order)";
        case mixer::Engine::SpatialRender::speakers:
        {
            // A standard array by its name, when the editor knows it for that many channels.
            const auto name = appSettings().value (key ("speakers")).toString().toStdString();
            const auto choices = dsp::layoutChoices ((int) spatialSpeakers.size());
            return std::find (choices.begin(), choices.end(), name) != choices.end() && name != dsp::speakerLayout ((int) spatialSpeakers.size()).name
                       ? name : std::string();
        }
        default: return {};
    }
}

void MixerWindow::loadSpatial (const QJsonObject& o)
{
    sceneYaw = std::clamp (o.value ("yaw").toDouble(), -180.0, 180.0);
    const auto render = o.value ("mixDown").toString();
    engine.spatialRender = render == QLatin1String ("speakers") ? mixer::Engine::SpatialRender::speakers
                         : render == QLatin1String ("ambix")    ? mixer::Engine::SpatialRender::ambix
                                                                : mixer::Engine::SpatialRender::headphones;
    setSpatialMode (o.value ("on").toBool());
}
