// SPDX-License-Identifier: AGPL-3.0-only
#include "AmbiStudio.h"
#include "AmbiScene.h"
#include "Settings.h"
#include "Theme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double previewSecondsMax = 15.0;

    QColor voiceColour (int voice)
    {
        static const QColor colours[] { QColor (0x4f, 0xc3, 0xf7), QColor (0xff, 0x8a, 0x65), QColor (0xae, 0xd5, 0x81), QColor (0xf0, 0x62, 0x92),
                                        QColor (0xff, 0xd5, 0x4f), QColor (0x9f, 0xa8, 0xda), QColor (0x4d, 0xb6, 0xac), QColor (0xce, 0x93, 0xd8) };
        return colours[(size_t) std::abs (voice) % 8];
    }

    // Factory racks, in the spirit of E4L's example sets.
    std::vector<std::pair<QString, studio::Scene>> factoryPresets()
    {
        using studio::Kind;
        std::vector<std::pair<QString, studio::Scene>> all;
        const auto with = [] (Kind k, std::initializer_list<std::pair<const char*, double>> values)
        {
            auto d = studio::makeDevice (k);
            for (const auto& [id, v] : values)
                d.set (id, v);
            return d;
        };
        {
            studio::Scene s;
            s.chain = { with (Kind::sourcePanner, {}) };
            all.emplace_back (QObject::tr ("Stereo in front"), s);
        }
        {
            studio::Scene s;
            s.chain = { with (Kind::monoPanner, { { "azLfo", 1 }, { "azTime", 1 }, { "azSync", 5 } }) };
            all.emplace_back (QObject::tr ("Orbit (one turn every two bars)"), s);
        }
        {
            studio::Scene s;
            s.chain = { with (Kind::quadPanner, { { "spreadMode", 1 }, { "spreadAzimuth", 90 } }),
                        with (Kind::convolutionReverb, { { "mix", 25 } }) };
            all.emplace_back (QObject::tr ("Quad upmix in a hall"), s);
        }
        {
            studio::Scene s;
            s.tailSeconds = 4.0;
            s.chain = { with (Kind::sourcePanner, { { "stereo", 0 } }),
                        with (Kind::multiDelay, { { "timeMode", 1 }, { "sixteenths", 3 }, { "voices", 8 }, { "azimuthMode", 1 }, { "spread", 45 } }) };
            all.emplace_back (QObject::tr ("Spiral echoes"), s);
        }
        {
            studio::Scene s;
            s.tailSeconds = 4.0;
            s.chain = { with (Kind::sourcePanner, { { "stereo", 0 } }),
                        with (Kind::multiDelay, { { "timeMode", 1 }, { "sixteenths", 2 }, { "voices", 10 }, { "pattern", 1 }, { "spread", 30 }, { "feedback", 35 } }) };
            all.emplace_back (QObject::tr ("Ping-pong all round"), s);
        }
        {
            studio::Scene s;
            s.tailSeconds = 3.0;
            s.chain = { with (Kind::brownianDelay, { { "shift", 12 }, { "voiceMode", 1 }, { "step", 0.5 } }) };
            all.emplace_back (QObject::tr ("Brownian cloud"), s);
        }
        {
            studio::Scene s;
            s.tailSeconds = 4.0;
            s.chain = { with (Kind::delayBoids, { { "boids", 12 }, { "speed", 0.8 } }) };
            all.emplace_back (QObject::tr ("Flock of echoes"), s);
        }
        {
            studio::Scene s;
            s.tailSeconds = 5.0;
            s.chain = { with (Kind::sourcePanner, {}), with (Kind::auraVerb, { { "t60Mid", 3.5 }, { "t60Low", 4.5 } }) };
            all.emplace_back (QObject::tr ("Stereo with AuraVerb"), s);
        }
        {
            studio::Scene s;
            s.chain = { with (Kind::spinner, { { "spinRate", 0.4 } }), with (Kind::spatialSlicer, { { "probability", 70 } }) };
            all.emplace_back (QObject::tr ("Spinner through a slicer"), s);
        }
        {
            studio::Scene s;
            s.chain = { with (Kind::bformatInput, {}), with (Kind::hoaTransform, {}) };
            all.emplace_back (QObject::tr ("Ambisonic file: turn the scene"), s);
        }
        return all;
    }

    // A number with a slider beside it.
    class NumberControl final : public QWidget
    {
    public:
        NumberControl (const studio::ParamSpec& param, double value, std::function<void (double)> changed, QWidget* parent)
            : QWidget (parent), spec (param), onChange (std::move (changed))
        {
            auto* row = new QHBoxLayout (this);
            row->setContentsMargins (0, 0, 0, 0);
            slider = new QSlider (Qt::Horizontal, this);
            slider->setRange (0, 1000);
            slider->setAccessibleName (QString::fromStdString (spec.label));
            box = new QDoubleSpinBox (this);
            box->setRange (spec.min, spec.max);
            box->setDecimals (spec.decimals);
            box->setSingleStep (spec.decimals == 0 || spec.max - spec.min > 50 ? 1.0 : std::pow (10.0, -std::min (spec.decimals, 2)));
            if (! spec.unit.empty())
                box->setSuffix (QStringLiteral (" ") + QString::fromStdString (spec.unit));
            box->setAccessibleName (QString::fromStdString (spec.label));
            box->setMinimumWidth (96);
            row->addWidget (slider, 1);
            row->addWidget (box);
            set (value);
            connect (slider, &QSlider::valueChanged, this, [this] (int v)
            {
                if (updating) return;
                const double x = this->spec.min + (this->spec.max - this->spec.min) * v / 1000.0;
                box->setValue (x);
            });
            connect (box, &QDoubleSpinBox::valueChanged, this, [this] (double v)
            {
                if (updating) return;
                updating = true;
                slider->setValue ((int) std::lround ((v - this->spec.min) / (this->spec.max - this->spec.min) * 1000.0));
                updating = false;
                this->onChange (v);
            });
            if (! spec.help.empty())
                setToolTip (QString::fromStdString (spec.help));
        }

        void set (double v)
        {
            updating = true;
            box->setValue (v);
            slider->setValue ((int) std::lround ((v - spec.min) / (spec.max - spec.min) * 1000.0));
            updating = false;
        }

    private:
        studio::ParamSpec spec;
        std::function<void (double)> onChange;
        QSlider* slider;
        QDoubleSpinBox* box;
        bool updating = false;
    };

    // The levels of a stretch of the scene, in dB, per channel.
    std::vector<float> channelLevels (const AudioData& d, Frame at, int frames)
    {
        std::vector<std::vector<float>> c ((size_t) d.numChannels(), std::vector<float> ((size_t) frames));
        std::vector<float*> p;
        for (auto& v : c)
            p.push_back (v.data());
        d.read (at, frames, p.data());
        std::vector<float> out;
        for (const auto& v : c)
        {
            double e = 0;
            for (auto x : v)
                e += (double) x * x;
            out.push_back ((float) (10.0 * std::log10 (e / frames + 1e-12)));
        }
        return out;
    }
}

//==============================================================================
StudioHud::StudioHud (QWidget* parent) : QWidget (parent)
{
    setMinimumSize (320, 170);
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (tr ("Heads-up display"));
    setToolTip (tr ("The room from above (front at the top) and from the side. Drag the bright sound to move it"));
}

void StudioHud::setDots (const std::vector<std::vector<studio::Dot>>& dots, int sel, bool drag)
{
    all = dots;
    selected = sel;
    draggable = drag;
    update();
}

QRectF StudioHud::topArea() const
{
    const double s = std::min (height() - 8.0, width() * 0.6 - 8.0);
    return { 4.0, (height() - s) / 2.0, s, s };
}

QRectF StudioHud::sideArea() const
{
    const auto top = topArea();
    const double left = top.right() + 12.0;
    const double s = std::min (width() - left - 4.0, top.height());
    return { left, (height() - s) / 2.0, std::max (10.0, s), std::max (10.0, s) };
}

void StudioHud::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    const auto line = theme.color (QStringLiteral ("mid"));
    const auto text = theme.color (QStringLiteral ("placeholderText"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);

    // From above: the unit circle, the listener with a nose at the front.
    const auto top = topArea();
    const auto c = top.center();
    const double R = top.width() / 2.0 - 10.0;
    g.setPen (QPen (line, 1.0));
    g.setBrush (Qt::NoBrush);
    g.drawEllipse (c, R, R);
    g.setPen (QPen (line, 1.0, Qt::DotLine));
    g.drawEllipse (c, R * 0.5, R * 0.5);
    g.drawLine (QPointF (c.x() - R, c.y()), QPointF (c.x() + R, c.y()));
    g.drawLine (QPointF (c.x(), c.y() - R), QPointF (c.x(), c.y() + R));
    g.setPen (QPen (text, 1.5));
    g.drawEllipse (c, 6.0, 6.0);
    g.drawLine (QPointF (c.x(), c.y() - 6), QPointF (c.x(), c.y() - 10));
    g.setPen (text);
    g.drawText (QRectF (top.left(), top.top() - 2, top.width(), 12), Qt::AlignHCenter, tr ("front"));

    // From the side (front to the right): elevation.
    const auto side = sideArea();
    const auto sc = side.center();
    const double SR = side.width() / 2.0 - 10.0;
    g.setPen (QPen (line, 1.0));
    g.drawArc (QRectF (sc.x() - SR, sc.y() - SR, SR * 2, SR * 2), 0, 360 * 16);
    g.setPen (QPen (line, 1.0, Qt::DotLine));
    g.drawLine (QPointF (sc.x() - SR, sc.y()), QPointF (sc.x() + SR, sc.y()));
    g.setPen (text);
    g.drawText (QRectF (side.left(), side.top() - 2, side.width(), 12), Qt::AlignHCenter, tr ("above"));
    g.drawText (QRectF (side.left(), side.bottom() - 12, side.width(), 12), Qt::AlignHCenter, tr ("side view, front →"));

    const auto draw = [&] (const studio::Dot& d, bool bright)
    {
        const double az = d.azimuth * M_PI / 180.0, el = d.elevation * M_PI / 180.0;
        const double r = std::sqrt (std::clamp ((double) d.radius, 0.0, 1.0));   // square-root scale: near sounds stay visible
        // Above: the distance on the floor is radius x cos(elevation); a sound at radius 0 sits on the head.
        const double flat = std::max (0.06, r) * std::cos (el);
        const QPointF p (c.x() + R * flat * std::sin (az), c.y() - R * flat * std::cos (az));
        const QPointF q (sc.x() + SR * std::max (0.06, r) * std::cos (el) * std::cos (az), sc.y() - SR * std::max (0.06, r) * std::sin (el));
        auto colour = voiceColour (d.voice);
        colour.setAlphaF (bright ? std::clamp (0.35 + 0.65 * d.level, 0.35, 1.0) : 0.25);
        g.setPen (Qt::NoPen);
        g.setBrush (colour);
        const double size = bright ? 6.0 : 4.0;
        g.drawEllipse (p, size, size);
        g.drawEllipse (q, size, size);
    };
    for (size_t i = 0; i < all.size(); ++i)
        if ((int) i != selected)
            for (const auto& d : all[i])
                draw (d, false);
    if (selected >= 0 && selected < (int) all.size())
        for (const auto& d : all[(size_t) selected])
            draw (d, true);
}

void StudioHud::dragTo (QPointF at, bool top)
{
    if (top)
    {
        const auto area = topArea();
        const auto c = area.center();
        const double R = area.width() / 2.0 - 10.0;
        const double dx = (at.x() - c.x()) / R, dy = (c.y() - at.y()) / R;
        const double shown = std::clamp (std::sqrt (dx * dx + dy * dy), 0.0, 1.0);
        Q_EMIT moved (std::atan2 (dx, dy) * 180.0 / M_PI, 0.0, shown * shown, true);
    }
    else
    {
        const auto area = sideArea();
        const auto c = area.center();
        Q_EMIT moved (0.0, std::clamp (std::atan2 (c.y() - at.y(), std::abs (at.x() - c.x()) + 1e-9) * 180.0 / M_PI, -90.0, 90.0), 0.0, false);
    }
}

void StudioHud::mousePressEvent (QMouseEvent* e)
{
    if (! draggable)
        return;
    dragging = topArea().contains (e->position()) ? 1 : sideArea().contains (e->position()) ? 2 : 0;
    if (dragging != 0)
        dragTo (e->position(), dragging == 1);
}

void StudioHud::mouseMoveEvent (QMouseEvent* e)
{
    if (draggable && dragging != 0 && (e->buttons() & Qt::LeftButton))
        dragTo (e->position(), dragging == 1);
}

//==============================================================================
StudioMeters::StudioMeters (QWidget* parent) : QWidget (parent)
{
    setMinimumSize (160, 90);
    setAccessibleName (tr ("Scene channel meters"));
    setToolTip (tr ("The level of each ambisonic channel of the scene: W, then Y Z X, then the higher orders"));
}

void StudioMeters::setLevels (const std::vector<float>& dbs)
{
    levels = dbs;
    update();
}

void StudioMeters::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    const auto text = theme.color (QStringLiteral ("placeholderText"));
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.7);
    g.setFont (small);
    const int count = std::max<int> (4, (int) levels.size());
    const double w = (width() - 8.0) / count;
    const double h = height() - 16.0;
    for (int i = 0; i < count; ++i)
    {
        const QRectF bar (4 + i * w + 1, 4, w - 2, h);
        g.fillRect (bar, theme.color (QStringLiteral ("mid")).darker (160));
        if (i < (int) levels.size())
        {
            const double v = std::clamp ((levels[(size_t) i] + 60.0) / 60.0, 0.0, 1.0);
            const QColor colour = levels[(size_t) i] > -1.0 ? QColor (0xe5, 0x39, 0x35) : levels[(size_t) i] > -12.0 ? QColor (0xfd, 0xd8, 0x35) : QColor (0x66, 0xbb, 0x6a);
            g.fillRect (QRectF (bar.left(), bar.bottom() - bar.height() * v, bar.width(), bar.height() * v), colour);
        }
        g.setPen (text);
        g.drawText (QRectF (bar.left() - 2, height() - 12, bar.width() + 4, 12), Qt::AlignHCenter, QString::fromStdString (ambi::channelName (i)));
    }
}

//==============================================================================
AmbiStudio::AmbiStudio (Host h, QWidget* parent) : QDialog (parent, Qt::Window), host (std::move (h))
{
    setWindowTitle (tr ("Ambisonic Studio"));
    setAccessibleName (tr ("Ambisonic Studio"));
    setAttribute (Qt::WA_DeleteOnClose, false);
    resize (1180, 640);

    // The last rack, or a source panner to start with.
    const auto saved = appSettings().value (QStringLiteral ("ambiStudio/scene")).toString().toStdString();
    if (saved.empty() || ! studio::fromJson (saved, scene))
        scene.chain = { studio::makeDevice (studio::Kind::sourcePanner) };
    // The file's own tempo, when it has a beat grid.
    if (Input in; host.input && host.input (in) && in.bpm > 0.0)
        scene.bpm = in.bpm;
    if (scene.master.hrtfPath.empty() && ! host.hrtfFiles.isEmpty())
        scene.master.hrtfPath = host.hrtfFiles.first().toStdString();

    auto* outer = new QVBoxLayout (this);
    auto* split = new QSplitter (Qt::Horizontal, this);
    outer->addWidget (split, 1);

    auto* left = new QWidget (split);
    auto* leftLayout = new QVBoxLayout (left);
    buildRack (left, leftLayout);

    auto* middle = new QWidget (split);
    auto* middleLayout = new QVBoxLayout (middle);
    deviceTitle = new QLabel (middle);
    auto bigger = deviceTitle->font();
    bigger.setBold (true);
    bigger.setPointSizeF (bigger.pointSizeF() * 1.2);
    deviceTitle->setFont (bigger);
    deviceText = new QLabel (middle);
    deviceText->setWordWrap (true);
    hud = new StudioHud (middle);
    editorScroll = new QScrollArea (middle);
    editorScroll->setWidgetResizable (true);
    middleLayout->addWidget (deviceTitle);
    middleLayout->addWidget (deviceText);
    middleLayout->addWidget (hud, 2);
    middleLayout->addWidget (editorScroll, 3);

    auto* right = new QWidget (split);
    auto* rightLayout = new QVBoxLayout (right);
    buildMaster (right, rightLayout);

    split->setStretchFactor (0, 0);
    split->setStretchFactor (1, 1);
    split->setStretchFactor (2, 0);
    split->setSizes ({ 240, 600, 320 });

    status = new QLabel (this);
    status->setWordWrap (true);
    outer->addWidget (status);

    connect (hud, &StudioHud::moved, this, [this] (double az, double el, double radius, bool top)
    {
        if (current < 0 || current >= (int) scene.chain.size())
            return;
        auto& d = scene.chain[(size_t) current];
        const bool xyz = d.kind == studio::Kind::sourcePanner && d.get ("coords") > 0.5;
        if (xyz)
        {
            // Keep the other coordinates where they are.
            double x = d.get ("x"), y = d.get ("y"), z = d.get ("z");
            if (top)
            {
                const double flat = radius;
                x = flat * std::sin (az * M_PI / 180.0);
                y = flat * std::cos (az * M_PI / 180.0);
            }
            else
            {
                const double flat = std::max (0.05, std::sqrt (x * x + y * y));
                z = std::clamp (flat * std::tan (el * M_PI / 180.0), -1.0, 1.0);
            }
            d.set ("x", x); d.set ("y", y); d.set ("z", z);
        }
        else if (top)
        {
            d.set ("azimuth", az);
            d.set ("radius", radius);
        }
        else
            d.set ("elevation", el);
        for (const auto* id : { "azimuth", "elevation", "radius", "x", "y", "z" })
            if (const auto it = setters.find (id); it != setters.end())
                it->second (d.get (id));
        changed();
    });

    connect (&watcher, &QFutureWatcher<std::shared_ptr<Preview>>::finished, this, [this]
    {
        auto made = watcher.result();
        if (renderAgain)
        {
            renderAgain = false;
            startPreview();
            return;
        }
        if (! previewBox->isChecked() || made == nullptr)
            return;
        if (made->heard == nullptr)
        {
            status->setText (QString::fromStdString (made->error.empty() ? made->result.error : made->error));
            return;
        }
        preview = made;
        status->setText (made->result.error.empty() ? tr ("Previewing %1 s, looping. Changes are heard as you make them.").arg ((double) (made->end - made->start) / made->heard->sampleRate(), 0, 'f', 1)
                                                     : QString::fromStdString (made->result.error));
        host.preview (made->heard, made->start, made->end);
    });

    previewTimer.setSingleShot (true);
    previewTimer.setInterval (250);
    connect (&previewTimer, &QTimer::timeout, this, [this] { if (previewBox->isChecked()) startPreview(); });
    frameTimer.setInterval (33);
    connect (&frameTimer, &QTimer::timeout, this, [this] { tick(); });
    frameTimer.start();

    refreshRack();
    rack->setCurrentRow (scene.chain.empty() ? -1 : 0);
    showDevice (scene.chain.empty() ? -1 : 0);
    status->setText (tr ("Build a rack: a panner places your sound in the sphere, effects work on the whole scene. Turn on Preview to hear it, then Apply."));
}

AmbiStudio::~AmbiStudio()
{
    if (cancelRender != nullptr)
        cancelRender->store (true);
    watcher.waitForFinished();
}

void AmbiStudio::buildRack (QWidget* parent, QVBoxLayout* into)
{
    auto* label = new QLabel (tr ("&Rack"), parent);
    rack = new QListWidget (parent);
    rack->setAccessibleName (tr ("Device rack"));
    rack->setToolTip (tr ("The devices in order: tick to turn one on or off, Delete removes it, Alt+Up/Down moves it"));
    label->setBuddy (rack);
    into->addWidget (label);
    into->addWidget (rack, 1);

    auto* add = new QPushButton (tr ("&Add Device"), parent);
    auto* menu = new QMenu (add);
    auto* sources = menu->addMenu (tr ("Sources and voices"));
    auto* effects = menu->addMenu (tr ("Scene effects"));
    for (const auto& d : studio::devices())
    {
        auto* action = (d.source ? sources : effects)->addAction (QString::fromStdString (d.name));
        action->setStatusTip (QString::fromStdString (d.description));
        action->setToolTip (QString::fromStdString (d.description));
        connect (action, &QAction::triggered, this, [this, k = d.kind] { addDevice (k); });
    }
    sources->setToolTipsVisible (true);
    effects->setToolTipsVisible (true);
    add->setMenu (menu);
    into->addWidget (add);

    auto* row = new QHBoxLayout();
    auto* up = new QToolButton (parent);
    up->setText (tr ("Up"));
    up->setAccessibleName (tr ("Move up"));
    auto* down = new QToolButton (parent);
    down->setText (tr ("Down"));
    down->setAccessibleName (tr ("Move down"));
    auto* copy = new QToolButton (parent);
    copy->setText (tr ("Copy"));
    copy->setAccessibleName (tr ("Duplicate device"));
    auto* remove = new QToolButton (parent);
    remove->setText (tr ("Remove"));
    for (auto* b : { up, down, copy, remove })
        row->addWidget (b);
    into->addLayout (row);

    const auto move = [this] (int by)
    {
        const int from = rack->currentRow(), to = from + by;
        if (from < 0 || to < 0 || to >= (int) scene.chain.size())
            return;
        std::swap (scene.chain[(size_t) from], scene.chain[(size_t) to]);
        refreshRack();
        rack->setCurrentRow (to);
        changed();
    };
    connect (up, &QToolButton::clicked, this, [move] { move (-1); });
    connect (down, &QToolButton::clicked, this, [move] { move (1); });
    connect (copy, &QToolButton::clicked, this, [this]
    {
        const int at = rack->currentRow();
        if (at < 0)
            return;
        scene.chain.insert (scene.chain.begin() + at + 1, scene.chain[(size_t) at]);
        refreshRack();
        rack->setCurrentRow (at + 1);
        changed();
    });
    connect (remove, &QToolButton::clicked, this, [this]
    {
        const int at = rack->currentRow();
        if (at < 0)
            return;
        scene.chain.erase (scene.chain.begin() + at);
        refreshRack();
        rack->setCurrentRow (std::min (at, (int) scene.chain.size() - 1));
        changed();
    });
    connect (rack, &QListWidget::currentRowChanged, this, [this] (int index) { showDevice (index); });
    connect (rack, &QListWidget::itemChanged, this, [this] (QListWidgetItem* item)
    {
        if (updating)
            return;
        const int at = rack->row (item);
        if (at >= 0 && at < (int) scene.chain.size())
        {
            scene.chain[(size_t) at].enabled = item->checkState() == Qt::Checked;
            changed();
        }
    });

    auto* presets = new QPushButton (tr ("&Presets"), parent);
    auto* presetMenu = new QMenu (presets);
    for (const auto& [name, s] : factoryPresets())
        connect (presetMenu->addAction (name), &QAction::triggered, this, [this, s = s] { loadPreset (s); });
    presetMenu->addSeparator();
    connect (presetMenu->addAction (tr ("Load Rack...")), &QAction::triggered, this, [this]
    {
        const auto path = QFileDialog::getOpenFileName (this, tr ("Load Rack"), QString(), tr ("Ambisonic Studio racks (*.json)"));
        if (path.isEmpty())
            return;
        QFile f (path);
        studio::Scene s;
        if (! f.open (QIODevice::ReadOnly) || ! studio::fromJson (f.readAll().toStdString(), s))
        {
            QMessageBox::warning (this, tr ("Load Rack"), tr ("%1 is not an Ambisonic Studio rack.").arg (QFileInfo (path).fileName()));
            return;
        }
        loadPreset (s);
    });
    connect (presetMenu->addAction (tr ("Save Rack...")), &QAction::triggered, this, [this]
    {
        auto path = QFileDialog::getSaveFileName (this, tr ("Save Rack"), QString(), tr ("Ambisonic Studio racks (*.json)"));
        if (path.isEmpty())
            return;
        if (! path.endsWith (QStringLiteral (".json")))
            path += QStringLiteral (".json");
        QFile f (path);
        if (! f.open (QIODevice::WriteOnly) || f.write (QByteArray::fromStdString (studio::toJson (scene))) < 0)
            QMessageBox::warning (this, tr ("Save Rack"), tr ("Could not write %1.").arg (path));
        else
            status->setText (tr ("Rack saved to %1").arg (QFileInfo (path).fileName()));
    });
    presets->setMenu (presetMenu);
    into->addWidget (presets);
}

void AmbiStudio::buildMaster (QWidget* parent, QVBoxLayout* into)
{
    auto* box = new QGroupBox (tr ("Master Bus"), parent);
    auto* form = new QFormLayout (box);
    into->addWidget (box);

    orderBox = new QComboBox (box);
    orderBox->addItems ({ tr ("1st order (4 channels)"), tr ("2nd order (9 channels)"), tr ("3rd order (16 channels)") });
    orderBox->setCurrentIndex (scene.order - 1);
    orderBox->setToolTip (tr ("The scene's ambisonic order: E4L works in 3rd order"));
    form->addRow (tr ("&Order:"), orderBox);

    bpmBox = new QDoubleSpinBox (box);
    bpmBox->setRange (20, 400);
    bpmBox->setDecimals (2);
    bpmBox->setSuffix (tr (" BPM"));
    bpmBox->setValue (scene.bpm);
    bpmBox->setToolTip (tr ("The tempo synced LFOs, delays and triggers follow (the file's beat grid, when it has one)"));
    form->addRow (tr ("&Tempo:"), bpmBox);

    tailBox = new QDoubleSpinBox (box);
    tailBox->setRange (0, 60);
    tailBox->setSuffix (tr (" s"));
    tailBox->setValue (scene.tailSeconds);
    tailBox->setToolTip (tr ("Added after the audio when applied, for echoes and reverb to ring out"));
    form->addRow (tr ("Ta&il:"), tailBox);

    meters = new StudioMeters (box);
    form->addRow (meters);
    scope = new AmbiScene (box);
    scope->setToolTip (tr ("HOA Scope: where the sound is in the scene, at the preview's playhead"));
    scope->setMinimumHeight (130);
    form->addRow (scope);

    masterGainBox = new QDoubleSpinBox (box);
    masterGainBox->setRange (-60, 24);
    masterGainBox->setSuffix (tr (" dB"));
    masterGainBox->setValue (scene.master.gainDb);
    form->addRow (tr ("&Gain:"), masterGainBox);

    decoderBox = new QComboBox (box);
    decoderBox->addItem (tr ("Ambisonics (AmbiX)"), QStringLiteral ("AmbiX"));
    decoderBox->addItem (tr ("Ambisonics (FuMa)"), QStringLiteral ("FuMa"));
    decoderBox->addItem (tr ("Headphones (binaural)"), QStringLiteral ("Headphones"));
    for (const auto& n : ambi::layoutNames())
        decoderBox->addItem (tr ("%1 speakers").arg (QString::fromStdString (n)), QString::fromStdString (n));
    decoderBox->setCurrentIndex (std::max (0, decoderBox->findData (QString::fromStdString (scene.master.decoder))));
    decoderBox->setToolTip (tr ("What Apply makes of the scene: an ambisonic file (to record or edit further), binaural for headphones, or speaker feeds"));
    form->addRow (tr ("&Decoder:"), decoderBox);

    hrtfBox = new QComboBox (box);
    for (const auto& f : host.hrtfFiles)
        hrtfBox->addItem (QFileInfo (f).completeBaseName(), f);
    hrtfBox->setCurrentIndex (std::max (0, hrtfBox->findData (QString::fromStdString (scene.master.hrtfPath))));
    hrtfBox->setToolTip (tr ("The HRTF set for headphones"));
    form->addRow (tr ("&HRTF:"), hrtfBox);

    monitorBox = new QComboBox (box);
    monitorBox->addItems ({ tr ("Headphones (binaural)"), tr ("Stereo speakers"), tr ("Decoder outputs 1+2") });
    monitorBox->setCurrentIndex (appSettings().value (QStringLiteral ("ambiStudio/monitor"), 0).toInt());
    monitorBox->setToolTip (tr ("How the preview is heard (E4L's Monitor 1+2)"));
    form->addRow (tr ("&Monitor:"), monitorBox);

    previewBox = new QCheckBox (tr ("&Preview"), box);
    previewBox->setToolTip (tr ("Hear the rack on the selection (or the first 15 s), looping"));
    applyButton = new QPushButton (tr ("A&pply to File"), box);
    applyButton->setToolTip (tr ("Render the whole file through the rack and the master bus (undoable)"));
    auto* row = new QHBoxLayout();
    row->addWidget (previewBox);
    row->addWidget (applyButton);
    into->addLayout (row);
    auto* close = new QPushButton (tr ("Close"), parent);
    into->addWidget (close);
    into->addStretch (1);

    connect (orderBox, &QComboBox::currentIndexChanged, this, [this] (int i) { scene.order = i + 1; changed(); });
    connect (bpmBox, &QDoubleSpinBox::valueChanged, this, [this] (double v) { scene.bpm = v; changed(); });
    connect (tailBox, &QDoubleSpinBox::valueChanged, this, [this] (double v) { scene.tailSeconds = v; save(); });
    connect (masterGainBox, &QDoubleSpinBox::valueChanged, this, [this] (double v) { scene.master.gainDb = v; changed(); });
    connect (decoderBox, &QComboBox::currentIndexChanged, this, [this]
    {
        scene.master.decoder = decoderBox->currentData().toString().toStdString();
        hrtfBox->setEnabled (scene.master.decoder == "Headphones" || monitorBox->currentIndex() == 0);
        changed();
    });
    connect (hrtfBox, &QComboBox::currentIndexChanged, this, [this] { scene.master.hrtfPath = hrtfBox->currentData().toString().toStdString(); changed(); });
    connect (monitorBox, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        appSettings().setValue (QStringLiteral ("ambiStudio/monitor"), i);
        changed();
    });
    connect (previewBox, &QCheckBox::toggled, this, [this] (bool on) { if (on) startPreview(); else stopPreview(); });
    connect (applyButton, &QPushButton::clicked, this, [this]
    {
        if (scene.chain.empty())
        {
            status->setText (tr ("The rack is empty: add a device first"));
            return;
        }
        previewBox->setChecked (false);
        save();
        host.apply (scene);
    });
    connect (close, &QPushButton::clicked, this, &QDialog::close);
}

void AmbiStudio::refreshRack()
{
    updating = true;
    rack->clear();
    for (const auto& d : scene.chain)
    {
        const auto& s = studio::spec (d.kind);
        auto* item = new QListWidgetItem ((s.source ? QStringLiteral ("◉ ") : QStringLiteral ("◇ ")) + QString::fromStdString (s.name), rack);
        item->setFlags (item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState (d.enabled ? Qt::Checked : Qt::Unchecked);
        item->setToolTip (QString::fromStdString (s.description));
    }
    updating = false;
}

void AmbiStudio::showDevice (int index)
{
    current = index;
    setters.clear();
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout (page);
    if (index < 0 || index >= (int) scene.chain.size())
    {
        deviceTitle->setText (tr ("No device"));
        deviceText->setText (tr ("Add a device: start with a panner (Source Panner for most sounds, B-Format Input for an ambisonic file)."));
        editorScroll->setWidget (page);
        return;
    }
    const auto& d = scene.chain[(size_t) index];
    const auto& s = studio::spec (d.kind);
    deviceTitle->setText (QString::fromStdString (s.name));
    deviceText->setText (QString::fromStdString (s.description));

    std::string group;
    QFormLayout* form = nullptr;
    for (const auto& p : s.params)
    {
        if (! studio::relevant (d, p.id))
            continue;
        if (form == nullptr || p.group != group)
        {
            group = p.group;
            auto* box = new QGroupBox (QString::fromStdString (group), page);
            form = new QFormLayout (box);
            layout->addWidget (box);
        }
        const auto label = QString::fromStdString (p.label) + QStringLiteral (":");
        const auto store = [this, index, id = p.id] (double v)
        {
            if (index < (int) scene.chain.size())
            {
                scene.chain[(size_t) index].set (id, v);
                changed();
            }
        };
        QWidget* control = nullptr;
        if (p.toggle)
        {
            auto* c = new QCheckBox (page);
            c->setChecked (d.get (p.id) > 0.5);
            connect (c, &QCheckBox::toggled, this, [this, store, index] (bool on)
            {
                store (on ? 1.0 : 0.0);
                QTimer::singleShot (0, this, [this, index] { if (current == index) showDevice (index); });
            });
            control = c;
        }
        else if (! p.choices.empty())
        {
            auto* c = new QComboBox (page);
            for (const auto& name : p.choices)
                c->addItem (QString::fromStdString (name));
            c->setCurrentIndex ((int) std::lround (d.get (p.id)));
            connect (c, &QComboBox::currentIndexChanged, this, [this, store, index] (int i)
            {
                store (i);
                QTimer::singleShot (0, this, [this, index] { if (current == index) showDevice (index); });
            });
            control = c;
        }
        else
        {
            auto* number = new NumberControl (p, d.get (p.id), store, page);
            setters[p.id] = [number] (double v) { number->set (v); };
            control = number;
        }
        if (! p.help.empty())
            control->setToolTip (QString::fromStdString (p.help));
        control->setAccessibleName (QString::fromStdString (p.label));
        form->addRow (label, control);
    }

    if (! s.files.empty())
    {
        auto* box = new QGroupBox (tr ("Impulse response files"), page);
        auto* files = new QFormLayout (box);
        layout->addWidget (box);
        auto* note = new QLabel (tr ("One 4-channel B-format file (AmbiX) in W, or a mono file for each of W, X, Y, Z (FuMa). None: a built-in hall."), box);
        note->setWordWrap (true);
        files->addRow (note);
        for (const auto& slot : s.files)
        {
            auto* row = new QWidget (box);
            auto* h = new QHBoxLayout (row);
            h->setContentsMargins (0, 0, 0, 0);
            auto* name = new QLineEdit (row);
            name->setReadOnly (true);
            const auto it = d.files.find (slot);
            name->setText (it == d.files.end() ? QString() : QString::fromStdString (it->second));
            name->setPlaceholderText (tr ("(built in)"));
            auto* browse = new QPushButton (tr ("Browse..."), row);
            auto* clear = new QToolButton (row);
            clear->setText (tr ("Clear"));
            h->addWidget (name, 1);
            h->addWidget (browse);
            h->addWidget (clear);
            connect (browse, &QPushButton::clicked, this, [this, index, slot, name]
            {
                const auto path = QFileDialog::getOpenFileName (this, tr ("Impulse Response %1").arg (QString::fromStdString (slot)), QString(),
                                                                tr ("Audio files (*.wav *.aif *.aiff *.flac *.w64 *.caf);;All files (*)"));
                if (path.isEmpty() || index >= (int) scene.chain.size())
                    return;
                scene.chain[(size_t) index].files[slot] = path.toStdString();
                name->setText (path);
                changed();
            });
            connect (clear, &QToolButton::clicked, this, [this, index, slot, name]
            {
                if (index >= (int) scene.chain.size())
                    return;
                scene.chain[(size_t) index].files.erase (slot);
                name->clear();
                changed();
            });
            files->addRow (QString::fromStdString (slot) + QStringLiteral (":"), row);
        }
    }
    layout->addStretch (1);
    const int scrolled = index == shownIndex ? editorScroll->verticalScrollBar()->value() : 0;
    editorScroll->setWidget (page);
    shownIndex = index;
    QTimer::singleShot (0, this, [this, scrolled] { editorScroll->verticalScrollBar()->setValue (scrolled); });
}

void AmbiStudio::addDevice (studio::Kind k)
{
    const int at = rack->currentRow() < 0 ? (int) scene.chain.size() : rack->currentRow() + 1;
    scene.chain.insert (scene.chain.begin() + at, studio::makeDevice (k));
    refreshRack();
    rack->setCurrentRow (at);
    changed();
}

void AmbiStudio::loadPreset (const studio::Scene& s)
{
    const auto master = scene.master;
    scene = s;
    scene.master.hrtfPath = master.hrtfPath;
    if (s.master.decoder == studio::Master().decoder)
        scene.master.decoder = master.decoder;
    updating = true;
    orderBox->setCurrentIndex (scene.order - 1);
    bpmBox->setValue (scene.bpm);
    tailBox->setValue (scene.tailSeconds);
    masterGainBox->setValue (scene.master.gainDb);
    decoderBox->setCurrentIndex (std::max (0, decoderBox->findData (QString::fromStdString (scene.master.decoder))));
    updating = false;
    refreshRack();
    rack->setCurrentRow (scene.chain.empty() ? -1 : 0);
    changed();
}

void AmbiStudio::changed (bool)
{
    if (updating)
        return;
    save();
    if (previewBox->isChecked())
        previewTimer.start();
}

void AmbiStudio::save() const
{
    appSettings().setValue (QStringLiteral ("ambiStudio/scene"), QString::fromStdString (studio::toJson (scene)));
}

void AmbiStudio::inputChanged()
{
    preview.reset();
    if (previewBox->isChecked())
        previewTimer.start();
}

void AmbiStudio::startPreview()
{
    if (watcher.isRunning())
    {
        renderAgain = true;
        if (cancelRender != nullptr)
            cancelRender->store (true);
        return;
    }
    Input in;
    if (! host.input (in) || in.data == nullptr)
    {
        status->setText (tr ("Open a file to hear the rack"));
        previewBox->setChecked (false);
        return;
    }
    const Frame end = std::min<Frame> (in.end, in.start + (Frame) (in.data->sampleRate() * previewSecondsMax));
    if (end <= in.start)
        return;

    auto s = scene;
    s.tailSeconds = 0.0;
    studio::Master monitor = s.master;
    const int mode = monitorBox->currentIndex();
    const bool haveHrtf = ! monitor.hrtfPath.empty();
    if (mode == 0)
        monitor.decoder = haveHrtf ? "Headphones" : "Stereo";
    else if (mode == 1 || monitor.decoder == "AmbiX" || monitor.decoder == "FuMa" || (monitor.decoder == "Headphones" && ! haveHrtf))
        monitor.decoder = "Stereo";
    if (mode == 0 && ! haveHrtf)
        status->setText (tr ("No HRTF set was found, so the preview is a stereo speaker decode"));

    cancelRender = std::make_shared<std::atomic<bool>> (false);
    status->setText (tr ("Rendering the preview..."));
    watcher.setFuture (QtConcurrent::run ([data = in.data, order = in.inputOrder, start = in.start, end, s, monitor, load = host.load, cancel = cancelRender]
    {
        auto p = std::make_shared<Preview>();
        p->start = start;
        p->end = end;
        const auto slice = data->slice (start, end);
        p->result = studio::render (*slice, order, s, load, nullptr, cancel.get());
        if (p->result.scene == nullptr)
            return p;
        auto heard = studio::decode (*p->result.scene, monitor, p->error);
        if (heard != nullptr && heard->numChannels() > 2)
        {
            // Outputs 1 and 2 of the decoder.
            ambi::Matrix pick (2, std::vector<double> ((size_t) heard->numChannels(), 0.0));
            pick[0][0] = pick[1][1] = 1.0;
            heard = ambi::mix (*heard, pick);
        }
        p->heard = heard;
        return p;
    }));
}

void AmbiStudio::stopPreview()
{
    previewTimer.stop();
    if (cancelRender != nullptr)
        cancelRender->store (true);
    renderAgain = false;
    preview.reset();
    host.preview (nullptr, 0, 0);
    status->setText (tr ("Preview off"));
}

void AmbiStudio::tick()
{
    if (! isVisible())
        return;
    const double t = preview != nullptr && host.previewSeconds ? host.previewSeconds() : -1.0;
    std::vector<std::vector<studio::Dot>> dots (scene.chain.size());
    if (t >= 0.0 && preview->result.scene != nullptr)
    {
        for (size_t i = 0; i < dots.size(); ++i)
            if (const auto* at = preview->result.motion.at (t, i))
                dots[i] = *at;
        const auto& sceneAudio = *preview->result.scene;
        const auto frame = (Frame) (t * sceneAudio.sampleRate());
        constexpr int frames = 2048;
        meters->setLevels (channelLevels (sceneAudio, std::max<Frame> (0, frame - frames / 2), frames));
        std::vector<std::vector<float>> channels ((size_t) sceneAudio.numChannels(), std::vector<float> (frames));
        std::vector<float*> p;
        for (auto& c : channels)
            p.push_back (c.data());
        sceneAudio.read (std::max<Frame> (0, frame - frames / 2), frames, p.data());
        scope->analyse (channels, true);
    }
    else
    {
        // Not heard: where the rack puts things at the start, from a moment of silence.
        const auto json = studio::toJson (scene);
        if (json != idleJson)
        {
            idleJson = json;
            auto still = scene;
            still.tailSeconds = 0.0;
            for (auto& d : still.chain)
                if (d.kind == studio::Kind::convolutionReverb)
                    d.enabled = false;   // nothing to see, and slow to set up
            AudioData::Builder b (2, 48000.0);
            std::vector<float> zero (2048, 0.0f);
            const float* z[] { zero.data(), zero.data() };
            b.append (z, 2048);
            const auto r = studio::render (*b.finish(), 0, still);
            idleDots = r.motion.frames.empty() ? std::vector<std::vector<studio::Dot>>() : r.motion.frames.front();
        }
        dots = idleDots;
        dots.resize (scene.chain.size());
        meters->setLevels ({});
    }
    const bool draggable = current >= 0 && current < (int) scene.chain.size() && studio::spec (scene.chain[(size_t) current].kind).hud;
    hud->setDots (dots, current, draggable);
}

void AmbiStudio::closeEvent (QCloseEvent* e)
{
    previewBox->setChecked (false);
    save();
    QDialog::closeEvent (e);
}

void AmbiStudio::keyPressEvent (QKeyEvent* e)
{
    if (rack->hasFocus() && e->key() == Qt::Key_Delete)
    {
        const int at = rack->currentRow();
        if (at >= 0)
        {
            scene.chain.erase (scene.chain.begin() + at);
            refreshRack();
            rack->setCurrentRow (std::min (at, (int) scene.chain.size() - 1));
            changed();
        }
        return;
    }
    if (rack->hasFocus() && (e->modifiers() & Qt::AltModifier) && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down))
    {
        const int from = rack->currentRow(), to = from + (e->key() == Qt::Key_Up ? -1 : 1);
        if (from >= 0 && to >= 0 && to < (int) scene.chain.size())
        {
            std::swap (scene.chain[(size_t) from], scene.chain[(size_t) to]);
            refreshRack();
            rack->setCurrentRow (to);
            changed();
        }
        return;
    }
    if (e->key() == Qt::Key_Escape)
    {
        close();
        return;
    }
    QDialog::keyPressEvent (e);
}
