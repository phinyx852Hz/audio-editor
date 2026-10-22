// SPDX-License-Identifier: AGPL-3.0-only
#include "MixerWindow.h"
#include "Potentiometer.h"
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
#include <QFileSystemModel>
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
#include <QShortcut>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

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

    // A channel fader: a long throw with the dB marks of its travel on the left (boost above
    // 0 dB, cut below), a cap to grab, dragged up and down (Shift ten times finer), a
    // double-click back to 0 dB.
    class Fader final : public QSlider
    {
    public:
        explicit Fader (QWidget* parent) : QSlider (Qt::Vertical, parent)
        {
            setRange (0, faderSteps);
            setFocusPolicy (Qt::NoFocus);
            setMinimumSize (60, 150);
            setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Expanding);
        }

        QSize sizeHint() const override { return { 60, 240 }; }

    protected:
        static constexpr double capWidth = 28.0, capHeight = 16.0, labels = 24.0;

        double grooveX() const { return labels + 6.0 + capWidth / 2.0; }
        double top() const { return capHeight / 2.0 + 3.0; }
        double bottom() const { return height() - capHeight / 2.0 - 3.0; }
        double yOf (int v) const { return top() + (1.0 - (double) v / faderSteps) * (bottom() - top()); }

        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            const auto pal = palette();
            const auto x = grooveX();
            auto f = font();
            f.setPointSizeF (std::max (6.5, f.pointSizeF() * 0.78));
            g.setFont (f);

            // The scale: boost above 0 dB, cut below; labels kept apart.
            auto ink = pal.color (QPalette::WindowText);
            ink.setAlpha (150);
            double lastY = -100.0;
            for (const auto db : { 6.0, 3.0, 0.0, -3.0, -6.0, -10.0, -15.0, -20.0, -30.0, -40.0, -60.0, -100.0 })
            {
                const auto y = yOf (faderPosition (db));
                if (std::abs (y - lastY) < 10.0 && db > -100.0)
                    continue;
                lastY = y;
                const bool unity = std::abs (db) < 0.01;
                g.setPen (unity ? pal.color (QPalette::Highlight) : ink);
                g.drawLine (QPointF (labels + 1.0, y), QPointF (labels + 5.0, y));
                const auto text = db <= -96.0 ? QStringLiteral ("-∞") : db > 0.0 ? QStringLiteral ("+%1").arg (db) : QString::number (db);
                g.drawText (QRectF (0.0, y - 7.0, labels - 1.0, 14.0), Qt::AlignRight | Qt::AlignVCenter, text);
            }

            // The groove, filled up to the cap.
            const auto capY = yOf (value());
            g.setPen (Qt::NoPen);
            g.setBrush (pal.color (QPalette::Mid));
            g.drawRoundedRect (QRectF (x - 2.5, top(), 5.0, bottom() - top()), 2.0, 2.0);
            auto fill = pal.color (isEnabled() ? QPalette::Highlight : QPalette::Mid);
            fill.setAlpha (170);
            g.setBrush (fill);
            g.drawRoundedRect (QRectF (x - 2.5, capY, 5.0, bottom() - capY), 2.0, 2.0);

            // The cap, with a line across where it reads.
            const QRectF cap (x - capWidth / 2.0, capY - capHeight / 2.0, capWidth, capHeight);
            g.setBrush (pal.color (QPalette::Button));
            g.setPen (QPen (isSliderDown() ? pal.color (QPalette::Highlight) : pal.color (QPalette::Mid), 1.0));
            g.drawRoundedRect (cap, 3.0, 3.0);
            g.setPen (QPen (pal.color (QPalette::WindowText), 1.5));
            g.drawLine (QPointF (cap.left() + 4.0, capY), QPointF (cap.right() - 4.0, capY));
        }

        void mousePressEvent (QMouseEvent* e) override
        {
            if (e->button() != Qt::LeftButton)
                return QSlider::mousePressEvent (e);
            // Off the cap, it jumps there first; then it follows the mouse.
            const auto y = e->position().y();
            if (std::abs (y - yOf (value())) > capHeight / 2.0 + 2.0)
                setValue ((int) std::lround ((1.0 - (y - top()) / std::max (1.0, bottom() - top())) * faderSteps));
            dragY = y;
            dragStart = value();
            setSliderDown (true);
            update();
            e->accept();
        }

        void mouseMoveEvent (QMouseEvent* e) override
        {
            if (! isSliderDown())
                return;
            const auto perPixel = faderSteps / std::max (1.0, bottom() - top()) * ((e->modifiers() & Qt::ShiftModifier) != 0 ? 0.1 : 1.0);
            setValue ((int) std::lround (dragStart + (dragY - e->position().y()) * perPixel));
            e->accept();
        }

        void mouseReleaseEvent (QMouseEvent* e) override
        {
            setSliderDown (false);
            update();
            e->accept();
        }

        void mouseDoubleClickEvent (QMouseEvent* e) override
        {
            setValue (faderPosition (0.0));
            e->accept();
        }

    private:
        double dragY = 0.0;
        int dragStart = 0;
    };

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

    QString timeText (double seconds)
    {
        const auto minutes = (int) (seconds / 60.0);
        return QStringLiteral ("%1:%2").arg (minutes).arg (seconds - minutes * 60.0, 4, 'f', 1, QLatin1Char ('0'));
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
    const auto bars = width() - (scale ? scaleWidth : 0);
    g.fillRect (QRect (0, 0, bars, height()), theme.color (QStringLiteral ("meterBackground")));
    g.fillRect (QRect (0, 0, bars, light - 1), clipped ? theme.color (QStringLiteral ("meterHigh")) : theme.color (QStringLiteral ("mid")));
    const auto top = light, height = std::max (1, this->height() - light);
    // -60 dB at the bottom, 0 dB at the top.
    auto yFor = [&] (float level)
    {
        const auto db = level <= 1.0e-6f ? -60.0 : std::clamp (20.0 * std::log10 (level), -60.0, 0.0);
        return top + (int) std::lround ((1.0 - (db + 60.0) / 60.0) * height);
    };
    const auto half = bars / 2;

    // dBFS beside the bars, labels kept apart.
    if (scale)
    {
        auto f = font();
        f.setPointSizeF (std::max (6.5, f.pointSizeF() * 0.78));
        g.setFont (f);
        auto ink = palette().color (QPalette::WindowText);
        ink.setAlpha (150);
        int lastY = -100;
        for (const auto db : { 0, -3, -6, -9, -12, -18, -24, -30, -40, -50, -60 })
        {
            const auto y = top + (int) std::lround ((1.0 - (db + 60.0) / 60.0) * height);
            if (y - lastY < 10)
                continue;
            lastY = y;
            g.setPen (db == 0 ? theme.color (QStringLiteral ("meterHigh")) : ink);
            g.drawLine (bars + 1, y, bars + 4, y);
            g.drawText (QRect (bars + 6, y - 7, scaleWidth - 6, 14), Qt::AlignLeft | Qt::AlignVCenter, QString::number (db));
        }
    }

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
// One track's strip, top to bottom: name, the instrument that its MIDI clips and the
// MIDI keyboard play, trim and polarity, pan, mute and solo, the fader and meter.
// The clips themselves are on the track's row above (TrackLanes).
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

        // What the MIDI clips play.
        instrumentBox = new QComboBox (this);
        instrumentBox->setFocusPolicy (Qt::NoFocus);
        instrumentBox->setToolTip (MixerWindow::tr ("What the track's MIDI clips and the MIDI keyboard play: the built-in Synth (with drums on channel 10), "
                                                    "a Sampler or Slicer of the track's own, or an instrument plugin"));
        editButton = smallButton (MixerWindow::tr ("Edit"), MixerWindow::tr ("Open the instrument's window (E)"), false);
        auto* instRow = new QHBoxLayout;
        instRow->addWidget (instrumentBox, 1);
        instRow->addWidget (editButton);
        layout->addLayout (instRow);

        transposeBox = new QSpinBox (this);
        transposeBox->setRange (-48, 48);
        transposeBox->setSuffix (MixerWindow::tr (" st"));
        transposeBox->setToolTip (MixerWindow::tr ("Transpose the MIDI clips (channel 10, the drums, stays)"));
        channelsButton = smallButton (MixerWindow::tr ("Ch"), MixerWindow::tr ("Which of the MIDI clips' channels play"), false);
        armButton = smallButton (QStringLiteral ("●"), MixerWindow::tr ("The MIDI keyboard plays this track's instrument (R)"), true);
        auto* midiOptions = new QHBoxLayout;
        midiOptions->addWidget (transposeBox, 1);
        midiOptions->addWidget (channelsButton);
        midiOptions->addWidget (armButton);
        layout->addLayout (midiOptions);

        // Pre: trim and polarity.
        trimDial = new Potentiometer (this);
        trimDial->setRange (-240, 240);
        trimDial->setFixedSize (40, 40);
        trimDial->setFocusPolicy (Qt::NoFocus);
        static_cast<Potentiometer*> (trimDial)->setOrigin (0);
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

        panDial = new Potentiometer (this);
        panDial->setRange (-100, 100);
        panDial->setFixedSize (40, 40);
        panDial->setFocusPolicy (Qt::NoFocus);
        static_cast<Potentiometer*> (panDial)->setOrigin (0);
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

        fader = new Fader (this);
        fader->setToolTip (MixerWindow::tr ("Fader, -∞ to +6 dB (Up and Down, Shift for finer steps; 0 resets; double-click the value to type one)"));
        meter = new LevelMeter (this);
        meter->setScale (true);
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
        uint16_t used = 0;
        bool anyMidi = false;
        for (const auto& c : window.engine.clips (index))
            if (c.midi != nullptr && c.midi->song != nullptr)
            {
                used |= c.midi->song->channelsUsed;
                anyMidi = true;
            }
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
            if ((used & (1u << ch)) != 0 && anyMidi)
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
        const bool hasSound = ! window.engine.clips (index).empty();
        menu.addAction (MixerWindow::tr ("Add &Audio File at the Playhead... (A)"), this, [this] { window.chooseAudio (index); });
        menu.addAction (MixerWindow::tr ("Add &MIDI File at the Playhead... (I)"), this, [this] { window.chooseMidi (index); });
        menu.addAction (MixerWindow::tr ("&Remove All Clips"), this, [this]
        {
            window.remember();
            window.editClips (index, {});
        })->setEnabled (hasSound);
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

    void dragEnterEvent (QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dropEvent (QDropEvent* e) override
    {
        QStringList files;
        for (const auto& url : e->mimeData()->urls())
            if (url.isLocalFile() && QFileInfo (url.toLocalFile()).isFile())
                files << url.toLocalFile();
        window.select (index);
        window.addFiles (index, files, window.engine.position());
        e->acceptProposedAction();
    }

public:
    MixerWindow& window;
    const int index;
    bool updating = false;
    QFrame* colourBar;
    QLabel* number;
    QLineEdit* nameEdit;
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
// The tracks across time, one horizontal row each, holding the track's clips: audio
// clips show their waveform, MIDI clips their notes. Clips are dragged with the mouse
// in time and between tracks (Ctrl copies; Shift leaves the grid), trimmed at their
// edges, and dropped on from the browser or the file manager. Keys (when the rows have
// the focus): Left/Right move the playhead a beat (Ctrl a bar), Up/Down pick the track,
// Delete removes the selected clips, Ctrl+E splits at the playhead, Ctrl+D duplicates,
// Shift+M mutes them, G turns the grid on and off. Ctrl+wheel zooms, the wheel scrolls.
class TrackLanes final : public QWidget
{
public:
    explicit TrackLanes (MixerWindow& w) : QWidget (&w), window (w)
    {
        setAcceptDrops (true);
        setFocusPolicy (Qt::ClickFocus);
        setMinimumHeight (120);
        setMouseTracking (true);
        setAccessibleName (MixerWindow::tr ("Tracks across time"));
    }

    QSize sizeHint() const override { return { 900, 260 }; }

    void clearSelection()
    {
        selection.clear();
        update();
    }

    bool snapping() const { return snap; }

    // Called by the window's timer: keeps the playhead in view while zoomed in.
    void follow()
    {
        if (fit || ! window.engine.isPlaying() || drag != Drag::none)
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
        g.setRenderHint (QPainter::Antialiasing, false);
        const auto pal = palette();
        g.fillRect (rect(), pal.color (QPalette::Base));
        updateScale();

        const int count = window.engine.numTracks();
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
        const auto barFrames = 4.0 * beatFrames();
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
            auto line = faint;
            line.setAlpha (labelled ? 46 : 24);
            g.setPen (line);
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
        std::set<const void*> live;
        for (int i = 0; i < count; ++i)
        {
            const QRect row (0, rulerHeight + i * rowH, width(), rowH);
            const auto& s = window.engine.strip (i);
            const auto colour = window.info[(size_t) i].colour;
            const bool heard = ! s.mute.load() && (! anySolo || s.solo.load());
            if (i == window.selected)
            {
                auto tint = colour;
                tint.setAlpha (26);
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
            if (window.info[(size_t) i].busy > 0)
                flags << MixerWindow::tr ("loading...");
            g.setFont (smallFont());
            g.setPen (softer);
            if (! flags.isEmpty() && rowH >= 34)
                g.drawText (header.adjusted (10, 0, -4, -3), Qt::AlignLeft | Qt::AlignBottom, flags.join (QLatin1Char (' ')));

            const QRect lane (headerWidth, row.top() + 2, timelineWidth(), rowH - 5);
            const auto list = window.engine.clips (i);
            g.save();
            g.setClipRect (lane);
            for (size_t k = 0; k < list.size(); ++k)
            {
                const auto& c = list[k];
                live.insert (c.audio != nullptr ? (const void*) c.audio.get() : (const void*) c.midi.get());
                drawClip (g, i, c, clipRect (i, list, k), lane, heard);
            }
            if (list.empty() && rowH >= 30)
            {
                g.setPen (softer);
                g.setFont (smallFont());
                g.drawText (lane.adjusted (8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                            window.info[(size_t) i].busy > 0 ? MixerWindow::tr ("Loading...")
                                                             : MixerWindow::tr ("Drop audio or MIDI files here, or press Enter on one in the browser"));
            }
            g.restore();
        }
        for (auto it = pictures.begin(); it != pictures.end();)
            it = live.count (it->first) == 0 ? pictures.erase (it) : std::next (it);

        // The playhead.
        const auto px = xOf ((double) window.engine.position());
        if (px >= headerWidth && px <= width())
        {
            g.setPen (QPen (Theme::current().color (QStringLiteral ("highlight")), 1.5));
            g.drawLine (QPointF (px, 0), QPointF (px, rulerHeight + count * rowH));
        }
        if (dropHint.x() >= headerWidth)
        {
            const int row = rowAt (dropHint.y());
            const auto x = xOf ((double) snapped (frameAt (dropHint.x()), false));
            if (row >= 0)
            {
                g.setPen (QPen (Theme::current().color (QStringLiteral ("highlight")), 2.0));
                g.drawLine (QPointF (x, rulerHeight + row * rowH), QPointF (x, rulerHeight + (row + 1) * rowH));
            }
        }
        if (hasFocus())
        {
            g.setPen (QPen (Theme::current().color (QStringLiteral ("highlight")), 1.0));
            g.drawRect (rect().adjusted (0, 0, -1, -1));
        }
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        setFocus();
        const auto pos = e->position();
        const int row = rowAt (pos.y());
        if (row >= 0 && pos.y() >= rulerHeight)
            window.select (row);
        drag = Drag::none;
        pressPos = pos;
        moved = false;
        if (e->button() != Qt::LeftButton || pos.x() < headerWidth)
        {
            update();
            return;
        }

        const auto hit = pos.y() >= rulerHeight ? clipAt (pos) : Hit {};
        if (hit.id == 0)
        {
            // Empty space or the ruler: the playhead goes there.
            if (! (e->modifiers() & Qt::ControlModifier))
                selection.clear();
            drag = Drag::scrub;
            locate (pos.x());
            return;
        }

        if (e->modifiers() & Qt::ControlModifier && hit.edge == Edge::none)
        {
            // Ctrl+click picks clips one by one; Ctrl+drag copies them.
            if (selection.count (hit.id) == 0)
                selection.insert (hit.id);
            else
                toggleOnRelease = hit.id;
        }
        else if (selection.count (hit.id) == 0)
            selection = { hit.id };
        before = window.snapshot();
        anchorId = hit.id;
        anchorTrack = hit.track;
        copying = (e->modifiers() & Qt::ControlModifier) != 0;
        drag = hit.edge == Edge::start ? Drag::trimStart : hit.edge == Edge::end ? Drag::trimEnd : Drag::move;
        update();
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        const auto pos = e->position();
        if (drag == Drag::none)
        {
            const auto hit = pos.y() >= rulerHeight && pos.x() >= headerWidth ? clipAt (pos) : Hit {};
            setCursor (hit.edge != Edge::none ? Qt::SizeHorCursor : Qt::ArrowCursor);
            return;
        }
        if (! (e->buttons() & Qt::LeftButton))
            return;
        if (drag == Drag::scrub)
        {
            locate (pos.x());
            return;
        }
        if (! moved && std::abs (pos.x() - pressPos.x()) < 3 && std::abs (pos.y() - pressPos.y()) < 3)
            return;
        if (! moved)
        {
            window.remember();
            toggleOnRelease = 0;
            if (copying && drag == Drag::move)
                makeCopies();
        }
        moved = true;
        const bool free = (e->modifiers() & Qt::ShiftModifier) != 0;
        const auto dx = (pos.x() - pressPos.x()) * framesPerPixel;
        if (drag == Drag::move)
            dragMove (dx, rowAt (pos.y()), free);
        else
            dragTrim (dx, free);
    }

    void mouseReleaseEvent (QMouseEvent*) override
    {
        if (drag == Drag::move || drag == Drag::trimStart || drag == Drag::trimEnd)
        {
            if (moved)
            {
                for (int i = 0; i < mixer::maxTracks; ++i)
                    if (touched[(size_t) i])
                        window.editClips (i, window.engine.clips (i));
            }
            else if (toggleOnRelease != 0)
                selection.erase (toggleOnRelease);
        }
        touched = {};
        toggleOnRelease = 0;
        drag = Drag::none;
        update();
    }

    void mouseDoubleClickEvent (QMouseEvent* e) override
    {
        if (e->position().y() < rulerHeight)
        {
            fit = true;
            update();
            return;
        }
        // An audio clip opens in the editor; the mixer steps aside until F3.
        const auto hit = clipAt (e->position());
        if (hit.id == 0)
            return;
        for (const auto& c : window.engine.clips (hit.track))
            if (c.id == hit.id)
            {
                if (c.audio != nullptr)
                    Q_EMIT window.editClipRequested (hit.track, hit.id);
                else
                    window.status->setText (MixerWindow::tr ("MIDI clips are not edited; R plays one backwards."));
            }
        drag = Drag::none;
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

    void keyPressEvent (QKeyEvent* e) override
    {
        const auto mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
        const auto key = e->key();
        const bool ctrl = mods == Qt::ControlModifier;
        if (mods == Qt::NoModifier || ctrl)
            switch (key)
            {
                case Qt::Key_Left:
                case Qt::Key_Right:
                {
                    const auto step = ctrl ? 4.0 * beatFrames() : beatFrames();
                    const auto now = (double) window.engine.position();
                    const auto next = key == Qt::Key_Right ? (std::floor (now / step + 1.0e-6) + 1.0) * step
                                                           : (std::ceil (now / step - 1.0e-6) - 1.0) * step;
                    window.engine.setPosition ((Frame) std::llround (std::max (0.0, next)));
                    window.refreshTransport();
                    showPlayhead();
                    update();
                    return;
                }
                case Qt::Key_Up:
                case Qt::Key_Down:
                    if (! ctrl)
                    {
                        const auto current = window.selected >= mixer::masterIndex ? 0 : window.selected;
                        window.select (std::clamp (current + (key == Qt::Key_Down ? 1 : -1), 0, window.engine.numTracks() - 1));
                        update();
                        return;
                    }
                    break;
                case Qt::Key_Delete:
                case Qt::Key_Backspace:
                    if (! ctrl)
                    {
                        deleteSelected();
                        return;
                    }
                    break;
                case Qt::Key_E:
                    if (ctrl)
                    {
                        split();
                        return;
                    }
                    break;
                case Qt::Key_D:
                    if (ctrl)
                    {
                        duplicate();
                        return;
                    }
                    break;
                case Qt::Key_G:
                    if (! ctrl)
                    {
                        toggleSnap();
                        return;
                    }
                    break;
                case Qt::Key_Escape:
                    clearSelection();
                    return;
                default: break;
            }
        if (mods == Qt::ShiftModifier && key == Qt::Key_M && ! selection.empty())
        {
            muteSelected();
            return;
        }
        if (mods == Qt::NoModifier && key == Qt::Key_R && ! selection.empty())
        {
            reverseSelected();
            return;
        }
        e->ignore();   // the window's keys: tracks, mute, solo, play...
    }

    void contextMenuEvent (QContextMenuEvent* e) override
    {
        const QPointF pos (e->pos());
        const int row = rowAt (pos.y());
        if (row < 0 || pos.y() < rulerHeight || pos.x() < headerWidth)
            return;
        window.select (row);
        const auto hit = clipAt (pos);
        QMenu menu (this);
        if (hit.id != 0)
        {
            if (selection.count (hit.id) == 0)
                selection = { hit.id };
            update();
            menu.addAction (MixerWindow::tr ("&Reverse (R)"), this, [this] { reverseSelected(); });
            menu.addAction (MixerWindow::tr ("&Mute or Unmute (Shift+M)"), this, [this] { muteSelected(); });
            menu.addAction (MixerWindow::tr ("&Split at the Playhead (Ctrl+E)"), this, [this] { split(); });
            menu.addAction (MixerWindow::tr ("D&uplicate (Ctrl+D)"), this, [this] { duplicate(); });
            menu.addAction (MixerWindow::tr ("&Delete (Delete)"), this, [this] { deleteSelected(); });
        }
        else
        {
            const auto at = snapped (frameAt (pos.x()), false);
            menu.addAction (MixerWindow::tr ("Add &Audio File Here..."), this, [this, row, at]
            {
                const auto file = QFileDialog::getOpenFileName (this, MixerWindow::tr ("Audio File for Track %1").arg (row + 1), window.folder(), audioFilter());
                if (! file.isEmpty())
                    window.addFiles (row, { file }, at);
            });
            menu.addAction (MixerWindow::tr ("Add &MIDI File Here..."), this, [this, row, at]
            {
                const auto file = QFileDialog::getOpenFileName (this, MixerWindow::tr ("MIDI File for Track %1").arg (row + 1), window.folder(),
                                                                MixerWindow::tr ("MIDI files (*.mid *.midi *.smf *.rmi);;All files (*)"));
                if (! file.isEmpty())
                    window.addFiles (row, { file }, at);
            });
        }
        menu.addSeparator();
        auto* grid = menu.addAction (MixerWindow::tr ("Snap to the &Grid (G)"), this, [this] { toggleSnap(); });
        grid->setCheckable (true);
        grid->setChecked (snap);
        menu.exec (e->globalPos());
    }

    void dragEnterEvent (QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dragMoveEvent (QDragMoveEvent* e) override
    {
        dropHint = e->position();
        e->acceptProposedAction();
        update();
    }

    void dragLeaveEvent (QDragLeaveEvent*) override
    {
        dropHint = QPointF (-1, -1);
        update();
    }

    void dropEvent (QDropEvent* e) override
    {
        dropHint = QPointF (-1, -1);
        const int row = rowAt (e->position().y());
        if (row < 0)
            return;
        QStringList files;
        for (const auto& url : e->mimeData()->urls())
            if (url.isLocalFile() && QFileInfo (url.toLocalFile()).isFile())
                files << url.toLocalFile();
        if (files.isEmpty())
            return;
        window.select (row);
        const auto free = (e->modifiers() & Qt::ShiftModifier) != 0;
        window.addFiles (row, files, snapped (frameAt (std::max ((double) headerWidth, e->position().x())), free));
        e->acceptProposedAction();
        update();
    }

    void focusInEvent (QFocusEvent* e) override
    {
        update();
        QWidget::focusInEvent (e);
    }

    void focusOutEvent (QFocusEvent* e) override
    {
        update();
        QWidget::focusOutEvent (e);
    }

private:
    static constexpr int headerWidth = 132;
    static constexpr int rulerHeight = 20;
    static constexpr int titleHeight = 13;
    static constexpr Frame peakBlock = 256;   // frames per waveform peak

    enum class Drag { none, scrub, move, trimStart, trimEnd };
    enum class Edge { none, start, end };
    struct Hit
    {
        uint32_t id = 0;
        int track = -1;
        Edge edge = Edge::none;
    };

    // Pictures of the files, made once for each.
    struct Picture
    {
        std::vector<std::pair<float, float>> peaks;   // min and max of both sides, per block
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

    double beatFrames() const { return 60.0 / window.tempoBox->value() * window.rate(); }
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
        const auto span = std::max ((double) window.engine.length() * 1.04, 16.0 * window.rate());
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
    Frame frameAt (double x) const { return (Frame) std::llround (std::max (0.0, start + (x - headerWidth) * framesPerPixel)); }

    // The grid: beats, or bars when beats would be too close together.
    Frame snapped (Frame frame, bool free) const
    {
        if (! snap || free)
            return std::max<Frame> (0, frame);
        auto grid = beatFrames();
        if (grid / framesPerPixel < 8.0)
            grid *= 4.0;
        return std::max<Frame> (0, (Frame) std::llround (std::round ((double) frame / grid) * grid));
    }

    void toggleSnap()
    {
        snap = ! snap;
        window.status->setText (snap ? MixerWindow::tr ("Clips snap to the grid (G turns it off; Shift while dragging leaves it)")
                                     : MixerWindow::tr ("Clips move freely (G snaps them to the grid again)"));
    }

    void locate (double x)
    {
        window.engine.setPosition (frameAt (x));
        window.refreshTransport();
        update();
    }

    void showPlayhead()
    {
        if (fit)
            return;
        const auto p = (double) window.engine.position();
        const auto span = framesPerPixel * timelineWidth();
        if (p < start || p > start + span * 0.95)
            start = std::max (0.0, p - span * 0.3);
    }

    // Clips that overlap share the row in layers, one above the other, so each stays in view.
    static std::vector<int> layers (const mixer::Clips& list, int& count)
    {
        std::vector<int> layer (list.size(), 0);
        std::vector<Frame> ends;   // where each layer is free again
        for (size_t k = 0; k < list.size(); ++k)
        {
            size_t l = 0;
            while (l < ends.size() && ends[l] > list[k].start)
                ++l;
            if (l == ends.size())
                ends.push_back (0);
            ends[l] = list[k].end();
            layer[k] = (int) l;
        }
        count = std::max (1, (int) ends.size());
        return layer;
    }

    QRectF clipRect (int track, const mixer::Clips& list, size_t k) const
    {
        int count = 1;
        const auto layer = layers (list, count)[k];
        const auto rowH = (double) rowHeight();
        const auto height = (rowH - 5.0) / count;
        const auto top = rulerHeight + track * rowH + 2.0 + layer * height;
        const auto& c = list[k];
        return { xOf ((double) c.start), top, std::max (2.0, (double) c.length / framesPerPixel), height - (count > 1 ? 1.0 : 0.0) };
    }

    Hit clipAt (QPointF pos) const
    {
        const int row = rowAt (pos.y());
        if (row < 0)
            return {};
        const auto list = window.engine.clips (row);
        for (auto k = list.size(); k-- > 0;)
        {
            const auto r = clipRect (row, list, k);
            if (! r.adjusted (-3, 0, 3, 0).contains (pos))
                continue;
            Hit hit { list[k].id, row, Edge::none };
            if (r.width() >= 14.0)
            {
                if (pos.x() < r.left() + 6.0)
                    hit.edge = Edge::start;
                else if (pos.x() > r.right() - 6.0)
                    hit.edge = Edge::end;
            }
            return hit;
        }
        return {};
    }

    // The clicked clip and the others selected move together, in time and across tracks.
    void dragMove (double dx, int row, bool free)
    {
        const mixer::Clip* anchor = nullptr;
        for (const auto& c : before[(size_t) anchorTrack])
            if (c.id == anchorId)
                anchor = &c;
        if (anchor == nullptr)
            return;
        const auto delta = snapped ((Frame) std::llround ((double) anchor->start + dx), free) - anchor->start;
        int lowest = mixer::maxTracks, highest = -1;
        for (int t = 0; t < mixer::maxTracks; ++t)
            for (const auto& c : before[(size_t) t])
                if (selection.count (c.id) != 0)
                {
                    lowest = std::min (lowest, t);
                    highest = std::max (highest, t);
                }
        auto shift = row < 0 ? 0 : row - anchorTrack;
        shift = std::clamp (shift, -lowest, window.engine.numTracks() - 1 - highest);

        std::array<mixer::Clips, mixer::maxTracks> lists;
        for (int t = 0; t < mixer::maxTracks; ++t)
            for (const auto& c : before[(size_t) t])
            {
                const bool picked = selection.count (c.id) != 0;
                if (! picked)
                    lists[(size_t) t].push_back (c);
                else
                {
                    auto shifted = c;
                    shifted.start = std::max<Frame> (0, c.start + delta);
                    lists[(size_t) (t + shift)].push_back (shifted);
                }
            }
        for (int t = 0; t < mixer::maxTracks; ++t)
            if (lists[(size_t) t].size() != before[(size_t) t].size() || shift != 0 || delta != 0 || touched[(size_t) t])
            {
                window.engine.setClips (t, lists[(size_t) t]);
                touched[(size_t) t] = true;
            }
        update();
    }

    // Ctrl+drag: the selected clips are copied where they are, and the copies are dragged.
    void makeCopies()
    {
        std::set<uint32_t> copies;
        uint32_t anchorCopy = 0;
        for (int t = 0; t < mixer::maxTracks; ++t)
        {
            auto& list = before[(size_t) t];
            const auto count = list.size();
            std::vector<uint32_t> from;
            for (size_t k = 0; k < count; ++k)
                if (selection.count (list[k].id) != 0)
                {
                    auto copy = list[k];
                    copy.id = 0;
                    list.push_back (copy);
                    from.push_back (list[k].id);
                }
            window.stamp (list);
            for (size_t k = count; k < list.size(); ++k)
            {
                copies.insert (list[k].id);
                if (from[k - count] == anchorId)
                    anchorCopy = list[k].id;
            }
        }
        selection = copies;
        anchorId = anchorCopy;
        copying = false;
    }

    // The clicked clip's start or end moves; the file under it stays where it is.
    void dragTrim (double dx, bool free)
    {
        auto list = before[(size_t) anchorTrack];
        for (auto& c : list)
        {
            if (c.id != anchorId)
                continue;
            const auto minimum = std::max<Frame> (1, (Frame) std::llround (framesPerPixel * 4.0));
            if (drag == Drag::trimStart)
            {
                const auto earliest = c.start - c.offset;   // where the file itself starts
                const auto at = std::clamp<Frame> (snapped ((Frame) std::llround ((double) c.start + dx), free), std::max<Frame> (0, earliest), c.end() - minimum);
                c.offset += at - c.start;
                c.length = c.end() - at;
                c.start = at;
            }
            else
            {
                const auto latest = c.start + (c.sourceLength() - c.offset);
                const auto at = std::clamp<Frame> (snapped ((Frame) std::llround ((double) c.end() + dx), free), c.start + minimum, latest);
                c.length = at - c.start;
            }
        }
        window.engine.setClips (anchorTrack, list);
        touched[(size_t) anchorTrack] = true;
        update();
    }

    void deleteSelected()
    {
        if (selection.empty())
            return;
        window.remember();
        for (int t = 0; t < window.engine.numTracks(); ++t)
        {
            auto list = window.engine.clips (t);
            const auto was = list.size();
            list.erase (std::remove_if (list.begin(), list.end(), [this] (const mixer::Clip& c) { return selection.count (c.id) != 0; }), list.end());
            if (list.size() != was)
                window.editClips (t, std::move (list));
        }
        selection.clear();
        update();
    }

    void muteSelected()
    {
        window.remember();
        for (int t = 0; t < window.engine.numTracks(); ++t)
        {
            auto list = window.engine.clips (t);
            bool any = false;
            for (auto& c : list)
                if (selection.count (c.id) != 0)
                {
                    c.muted = ! c.muted;
                    any = true;
                }
            if (any)
                window.editClips (t, std::move (list));
        }
    }

    // Backwards, or forwards again: the same part of the sound, played the other way.
    void reverseSelected()
    {
        window.remember();
        int count = 0;
        for (int t = 0; t < window.engine.numTracks(); ++t)
        {
            auto list = window.engine.clips (t);
            bool any = false;
            for (auto& c : list)
                if (selection.count (c.id) != 0)
                {
                    c.offset = c.sourceLength() - c.offset - c.length;
                    c.reversed = ! c.reversed;
                    if (c.midi != nullptr)
                        c.midi = mixer::reverseMidi (*c.midi);
                    any = true;
                    ++count;
                }
            if (any)
                window.editClips (t, std::move (list));
        }
        window.status->setText (MixerWindow::tr ("Reversed %n clip(s) (R again turns them back)", nullptr, count));
    }

    // At the playhead: the selected clips, or with none selected every clip of the track there.
    void split()
    {
        const auto at = window.engine.position();
        const auto track = window.selected < mixer::masterIndex ? window.selected : -1;
        bool remembered = false;
        for (int t = 0; t < window.engine.numTracks(); ++t)
        {
            auto list = window.engine.clips (t);
            mixer::Clips out;
            bool any = false;
            for (const auto& c : list)
            {
                const bool picked = selection.empty() ? t == track : selection.count (c.id) != 0;
                if (picked && at > c.start && at < c.end())
                {
                    auto left = c, right = c;
                    left.length = at - c.start;
                    right.start = at;
                    right.offset = c.offset + left.length;
                    right.length = c.length - left.length;
                    right.id = 0;
                    out.push_back (left);
                    out.push_back (right);
                    any = true;
                }
                else
                    out.push_back (c);
            }
            if (! any)
                continue;
            if (! remembered)
                window.remember();
            remembered = true;
            window.editClips (t, std::move (out));
        }
        if (! remembered)
            window.status->setText (MixerWindow::tr ("No clip to split at the playhead: put it over a clip and select the clip or its track."));
    }

    // Each selected clip again, right after itself.
    void duplicate()
    {
        if (selection.empty())
            return;
        window.remember();
        std::set<uint32_t> copies;
        for (int t = 0; t < window.engine.numTracks(); ++t)
        {
            auto list = window.engine.clips (t);
            const auto count = list.size();
            for (size_t k = 0; k < count; ++k)
                if (selection.count (list[k].id) != 0)
                {
                    auto copy = list[k];
                    copy.start = list[k].end();
                    copy.id = 0;
                    list.push_back (copy);
                }
            if (list.size() == count)
                continue;
            window.stamp (list);
            for (size_t k = count; k < list.size(); ++k)
                copies.insert (list[k].id);
            window.editClips (t, std::move (list));
        }
        selection = copies;
        update();
    }

    const Picture& picture (const mixer::Clip& c)
    {
        const void* key = c.audio != nullptr ? (const void*) c.audio.get() : (const void*) c.midi.get();
        if (auto it = pictures.find (key); it != pictures.end())
            return it->second;
        auto& p = pictures[key];
        if (c.audio != nullptr)
        {
            const auto& audio = *c.audio;
            const auto n = audio.length();
            p.peaks.reserve ((size_t) (n / peakBlock + 1));
            for (Frame at = 0; at < n; at += peakBlock)
            {
                float lo = 0.0f, hi = 0.0f;
                const auto end = std::min (n, at + peakBlock);
                for (Frame f = at; f < end; ++f)
                {
                    const auto l = audio.left[(size_t) f], r = audio.right[(size_t) f];
                    lo = std::min ({ lo, l, r });
                    hi = std::max ({ hi, l, r });
                }
                p.peaks.push_back ({ lo, hi });
            }
        }
        else if (c.midi != nullptr)
        {
            std::array<std::array<Frame, 128>, 16> on;
            for (auto& ch : on)
                ch.fill (-1);
            for (const auto& e : c.midi->events)
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
            for (int ch = 0; ch < 16; ++ch)
                for (int n = 0; n < 128; ++n)
                    if (on[(size_t) ch][(size_t) n] >= 0)
                        p.notes.push_back ({ on[(size_t) ch][(size_t) n], c.midi->length, n, ch });
            p.low = 127;
            p.high = 0;
            for (const auto& n : p.notes)
            {
                p.low = std::min (p.low, n.pitch);
                p.high = std::max (p.high, n.pitch);
            }
            if (p.low > p.high)
                p.low = p.high = 60;
        }
        return p;
    }

    void drawClip (QPainter& g, int track, const mixer::Clip& c, QRectF box, QRect lane, bool heard)
    {
        if (box.right() < lane.left() || box.left() > lane.right())
            return;
        const bool picked = selection.count (c.id) != 0;
        auto colour = window.info[(size_t) track].colour;
        if (c.muted || ! heard)
            colour = QColor::fromHsvF (colour.hsvHueF(), colour.hsvSaturationF() * 0.2f, colour.valueF() * 0.7f);
        auto fill = colour;
        fill.setAlpha (c.isMidi() ? 46 : 58);
        g.fillRect (box, fill);
        auto edge = picked ? Theme::current().color (QStringLiteral ("highlight")) : colour;
        g.setPen (QPen (edge, picked ? 2.0 : 1.0));
        g.drawRect (box.adjusted (0.5, 0.5, -0.5, -0.5));

        // The title: the file's name (the part heard, when trimmed).
        auto body = box.adjusted (1, 1, -1, -1);
        if (box.height() >= 28)
        {
            const QRectF title (box.left(), box.top(), box.width(), titleHeight);
            auto band = colour;
            band.setAlpha (150);
            g.fillRect (title, band);
            const auto source = c.audio != nullptr ? c.audio->path : c.midi->path;
            auto name = QFileInfo (QString::fromStdString (source)).completeBaseName();
            if (c.reversed)
                name = QStringLiteral ("◀ ") + name;
            if (c.muted)
                name = MixerWindow::tr ("%1 (muted)").arg (name);
            g.setPen (palette().color (QPalette::Base));
            auto f = smallFont();
            f.setPointSizeF (std::max (6.5, f.pointSizeF() * 0.95));
            g.setFont (f);
            const auto visibleLeft = std::max (title.left(), (double) lane.left());
            const QRectF label (visibleLeft + 4, title.top(), std::max (0.0, title.right() - visibleLeft - 6), title.height());
            g.drawText (label, Qt::AlignVCenter | Qt::AlignLeft, g.fontMetrics().elidedText (name, Qt::ElideRight, (int) label.width()));
            body.setTop (title.bottom() + 1);
        }

        auto ink = colour;
        ink.setAlpha (235);
        const auto& p = picture (c);
        if (c.audio != nullptr)
        {
            g.setPen (ink);
            const auto mid = body.center().y();
            const auto half = body.height() / 2.0 - 1.0;
            const auto from = std::max ((int) body.left(), lane.left());
            const auto to = std::min ((int) body.right(), lane.right());
            for (int x = from; x < to; ++x)
            {
                // The file's frames under this pixel.
                auto f0 = (double) c.offset + (start + (x - headerWidth) * framesPerPixel - (double) c.start);
                auto f1 = f0 + framesPerPixel;
                if (c.reversed)
                {
                    // The same frames, counted from the file's end.
                    const auto n = (double) c.sourceLength();
                    const auto a0 = n - f1, a1 = n - f0;
                    f0 = a0;
                    f1 = a1;
                }
                auto a = (size_t) std::max (0.0, std::floor (f0 / peakBlock));
                if (a >= p.peaks.size())
                    break;
                const auto b = std::min (p.peaks.size(), std::max (a + 1, (size_t) std::max (0.0, std::ceil (f1 / peakBlock))));
                float lo = 0.0f, hi = 0.0f;
                for (; a < b; ++a)
                {
                    lo = std::min (lo, p.peaks[a].first);
                    hi = std::max (hi, p.peaks[a].second);
                }
                g.drawLine (QPointF (x + 0.5, mid - std::min (1.0f, hi) * half), QPointF (x + 0.5, mid - std::max (-1.0f, lo) * half));
            }
        }
        else
        {
            const auto range = std::max (12, p.high - p.low + 1);
            const auto low = p.low - (range - (p.high - p.low + 1)) / 2;
            const auto noteH = std::max (1.0, body.height() / range);
            const auto channels = window.engine.strip (track).channels.load();
            auto muted = ink;
            muted.setAlpha (70);
            for (const auto& n : p.notes)
            {
                if (n.off <= c.offset || n.on >= c.offset + c.length)
                    continue;
                const auto on = std::max (n.on, c.offset), off = std::min (n.off, c.offset + c.length);
                const auto x0 = xOf ((double) (c.start + on - c.offset)), x1 = xOf ((double) (c.start + off - c.offset));
                if (x1 < lane.left() || x0 > lane.right())
                    continue;
                const auto y = body.bottom() - (n.pitch - low + 1) * body.height() / range;
                g.fillRect (QRectF (x0, y, std::max (1.5, x1 - x0), noteH), (channels & (1u << n.channel)) != 0 ? ink : muted);
            }
        }
    }

    MixerWindow& window;
    std::map<const void*, Picture> pictures;
    std::set<uint32_t> selection;
    bool fit = true;
    bool snap = true;
    double start = 0.0;            // the frame at the left edge of the timeline
    double framesPerPixel = 1000.0;

    // A drag in progress.
    Drag drag = Drag::none;
    QPointF pressPos;
    bool moved = false;
    bool copying = false;
    uint32_t anchorId = 0;         // the clip grabbed
    int anchorTrack = 0;
    uint32_t toggleOnRelease = 0;  // a Ctrl+click on a selected clip unselects it, if it was not dragged
    MixerWindow::Snapshot before;  // the clips when the drag began
    std::array<bool, mixer::maxTracks> touched {};
    QPointF dropHint { -1, -1 };
};

//==============================================================================
// The file browser beside the tracks: folders, audio files and MIDI files. Arrows walk
// it, Enter (or a double-click) puts the file on the selected track at the playhead and
// moves the playhead to its end, so files can be laid one after another; Shift+Enter
// leaves the playhead. Enter on a folder opens it, Backspace goes up. Files drag onto
// any track's row. 1-8 still pick the track and Space still plays.
class FileBrowser final : public QWidget
{
    // Arrow keys stay in the list even at its ends, rather than reaching the strips.
    class Tree final : public QTreeView
    {
    public:
        using QTreeView::QTreeView;

    protected:
        void keyPressEvent (QKeyEvent* e) override
        {
            QTreeView::keyPressEvent (e);
            switch (e->key())
            {
                case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
                case Qt::Key_PageUp: case Qt::Key_PageDown: case Qt::Key_Home: case Qt::Key_End:
                    e->accept();
                    break;
                default: break;
            }
        }
    };

public:
    explicit FileBrowser (MixerWindow& w) : QWidget (&w), window (w)
    {
        setMinimumWidth (180);
        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (0, 0, 0, 0);
        layout->setSpacing (3);

        places = new QComboBox (this);
        places->setFocusPolicy (Qt::NoFocus);
        places->setToolTip (MixerWindow::tr ("Places and favourite folders"));
        upButton = new QPushButton (QStringLiteral ("↑"), this);
        upButton->setFocusPolicy (Qt::NoFocus);
        upButton->setToolTip (MixerWindow::tr ("Up a folder (Backspace)"));
        upButton->setFixedWidth (28);
        favouriteButton = new QPushButton (QStringLiteral ("★"), this);
        favouriteButton->setFocusPolicy (Qt::NoFocus);
        favouriteButton->setCheckable (true);
        favouriteButton->setToolTip (MixerWindow::tr ("Keep this folder among the favourites"));
        favouriteButton->setFixedWidth (28);
        auto* top = new QHBoxLayout;
        top->addWidget (places, 1);
        top->addWidget (upButton);
        top->addWidget (favouriteButton);
        layout->addLayout (top);

        model = new QFileSystemModel (this);
        QStringList filters;
        for (const auto& ext : AudioEngine::readableExtensions())
            filters << QStringLiteral ("*.") + QString::fromStdString (ext);
        filters << QStringLiteral ("*.mid") << QStringLiteral ("*.midi") << QStringLiteral ("*.smf") << QStringLiteral ("*.rmi");
        model->setNameFilters (filters);
        model->setNameFilterDisables (false);
        model->setFilter (QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
        model->setRootPath (QDir::rootPath());

        tree = new Tree (this);
        tree->setModel (model);
        tree->setHeaderHidden (true);
        for (int c = 1; c < model->columnCount(); ++c)
            tree->hideColumn (c);
        tree->setDragEnabled (true);
        tree->setDragDropMode (QAbstractItemView::DragOnly);
        tree->setSelectionMode (QAbstractItemView::SingleSelection);
        tree->setUniformRowHeights (true);
        tree->setAccessibleName (MixerWindow::tr ("Files"));
        tree->installEventFilter (this);
        layout->addWidget (tree, 1);

        hint = new QLabel (this);
        hint->setWordWrap (true);
        hint->setEnabled (false);
        layout->addWidget (hint);

        favourites = appSettings().value (QStringLiteral ("mixer/favourites")).toStringList();
        fillPlaces();
        setRoot (appSettings().value (QStringLiteral ("mixer/browserRoot"), window.folder()).toString());

        QObject::connect (places, &QComboBox::activated, this, [this] (int i) { setRoot (places->itemData (i).toString()); tree->setFocus(); });
        QObject::connect (upButton, &QPushButton::clicked, this, [this] { goUp(); });
        QObject::connect (favouriteButton, &QPushButton::toggled, this, [this] (bool on)
        {
            favourites.removeAll (root);
            if (on)
                favourites << root;
            appSettings().setValue (QStringLiteral ("mixer/favourites"), favourites);
            fillPlaces();
        });
        QObject::connect (tree, &QTreeView::doubleClicked, this, [this] (const QModelIndex& index) { activate (index, true); });
        QObject::connect (tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { showHint(); });
    }

    QTreeView* view() const { return tree; }

    void showHint()
    {
        const auto track = window.selected < mixer::masterIndex ? window.selected + 1 : 1;
        hint->setText (MixerWindow::tr ("Enter puts the file on track %1 at the playhead").arg (track));
    }

protected:
    bool eventFilter (QObject* o, QEvent* e) override
    {
        if (o == tree && e->type() == QEvent::KeyPress)
        {
            auto* k = static_cast<QKeyEvent*> (e);
            const auto mods = k->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
            switch (k->key())
            {
                case Qt::Key_Return:
                case Qt::Key_Enter:
                    if (mods == Qt::NoModifier || mods == Qt::ShiftModifier)
                    {
                        activate (tree->currentIndex(), mods == Qt::NoModifier);
                        return true;
                    }
                    break;
                case Qt::Key_Backspace:
                    goUp();
                    return true;
                case Qt::Key_Space:
                    window.togglePlay();
                    return true;
                default:
                    break;
            }
            // The track keys, and the window's own Ctrl keys, still work from here.
            if ((mods == Qt::NoModifier && k->key() >= Qt::Key_1 && k->key() <= Qt::Key_9) || (mods & Qt::ControlModifier))
            {
                QCoreApplication::sendEvent (&window, k);
                showHint();
                return true;
            }
        }
        return QWidget::eventFilter (o, e);
    }

private:
    void fillPlaces()
    {
        const QSignalBlocker block (places);
        places->clear();
        auto add = [this] (const QString& name, const QString& folder)
        {
            if (! folder.isEmpty() && QFileInfo (folder).isDir() && places->findData (folder) < 0)
                places->addItem (name, folder);
        };
        add (MixerWindow::tr ("Home"), QDir::homePath());
        add (MixerWindow::tr ("Music"), QStandardPaths::writableLocation (QStandardPaths::MusicLocation));
        add (MixerWindow::tr ("Desktop"), QStandardPaths::writableLocation (QStandardPaths::DesktopLocation));
        add (MixerWindow::tr ("Downloads"), QStandardPaths::writableLocation (QStandardPaths::DownloadLocation));
        add (MixerWindow::tr ("Last used"), window.folder());
        for (const auto& f : favourites)
            add (QStringLiteral ("★ ") + QFileInfo (f).fileName(), f);
        if (! root.isEmpty())
        {
            if (places->findData (root) < 0)
                places->addItem (root, root);
            places->setCurrentIndex (places->findData (root));
        }
    }

    void setRoot (const QString& folder)
    {
        root = QFileInfo (folder).isDir() ? QFileInfo (folder).absoluteFilePath() : QDir::homePath();
        tree->setRootIndex (model->index (root));
        appSettings().setValue (QStringLiteral ("mixer/browserRoot"), root);
        {
            const QSignalBlocker block (favouriteButton);
            favouriteButton->setChecked (favourites.contains (root));
        }
        fillPlaces();
        places->setToolTip (root);
        QTimer::singleShot (0, this, [this]
        {
            if (! tree->currentIndex().isValid() || tree->currentIndex().parent() != tree->rootIndex())
                tree->setCurrentIndex (model->index (0, 0, tree->rootIndex()));
        });
        showHint();
    }

    void goUp()
    {
        QDir dir (root);
        const auto from = root;
        if (! dir.cdUp())
            return;
        setRoot (dir.absolutePath());
        tree->setCurrentIndex (model->index (from));
    }

    void activate (const QModelIndex& index, bool movePlayhead)
    {
        if (! index.isValid())
            return;
        const auto file = model->filePath (index);
        if (model->isDir (index))
        {
            setRoot (file);
            return;
        }
        const auto track = window.selected < mixer::masterIndex ? window.selected : 0;
        window.addFiles (track, { file }, window.engine.position(), movePlayhead);
    }

    MixerWindow& window;
    QComboBox* places;
    QPushButton* upButton;
    QPushButton* favouriteButton;
    QFileSystemModel* model;
    QTreeView* tree;
    QLabel* hint;
    QString root;
    QStringList favourites;
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
    masterFader = new Fader (masterStrip);
    masterFader->setToolTip (tr ("The master out (9 selects it; Up and Down move it)"));
    masterMeter = new LevelMeter (masterStrip);
    masterMeter->setScale (true);
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

    // The file browser on the left of it all.
    browser = new FileBrowser (*this);
    sideSplitter = new QSplitter (Qt::Horizontal, this);
    sideSplitter->setChildrenCollapsible (false);
    sideSplitter->addWidget (browser);
    sideSplitter->addWidget (splitter);
    sideSplitter->setStretchFactor (0, 0);
    sideSplitter->setStretchFactor (1, 1);
    sideSplitter->setSizes ({ 240, 1100 });
    sideSplitter->restoreState (appSettings().value (QStringLiteral ("mixer/sideSplitter")).toByteArray());
    browser->setVisible (appSettings().value (QStringLiteral ("mixer/browser"), true).toBool());

    // F6 and Tab go round the browser, the tracks and the strips.
    auto* nextSection = new QShortcut (QKeySequence (Qt::Key_F6), this);
    connect (nextSection, &QShortcut::activated, this, [this] { cycleFocus (true); });
    auto* previousSection = new QShortcut (QKeySequence (Qt::SHIFT | Qt::Key_F6), this);
    connect (previousSection, &QShortcut::activated, this, [this] { cycleFocus (false); });

    status = new QLabel (this);
    status->setWordWrap (true);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget (status, 1);
    bottom->addWidget (midiStatus);
    bottom->addWidget (midiButton);

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (top);
    layout->addWidget (sideSplitter, 1);
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
    if (browser != nullptr)
        browser->showHint();
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
    if ((! engine.clips (last).empty() || engine.instrument (last) != nullptr)
        && QMessageBox::question (this, tr ("Remove Track"), tr ("Remove track %1, “%2”, and what is on it?").arg (last + 1).arg (info[(size_t) last].name))
               != QMessageBox::Yes)
        return;
    remember();
    engine.setNumTracks (last);
    engine.setClips (last, {});
    engine.setInstrument (last, nullptr);
    auto& s = engine.strip (last);
    s.mute = s.solo = false;
    ++info[(size_t) last].generation;
    info[(size_t) last].busy = 0;
    if (info[(size_t) last].editor != nullptr)
        info[(size_t) last].editor->close();
    rebuildStrips();
    changed();
}

//==============================================================================
// Clips: files put on a track's timeline, edits to them, and undo.
void MixerWindow::editClips (int track, mixer::Clips list)
{
    stamp (list);
    engine.setClips (track, std::move (list));
    if (loopButton->isChecked())
        engine.setLoop (0, engine.length());
    strips[(size_t) track]->refresh();
    lanes->update();
    refreshTransport();
    if (! restoring)
        changed();
}

void MixerWindow::addClip (int track, mixer::Clip clip)
{
    auto list = engine.clips (track);
    list.push_back (std::move (clip));
    editClips (track, std::move (list));
}

MixerWindow::Snapshot MixerWindow::snapshot() const
{
    Snapshot s;
    for (int i = 0; i < mixer::maxTracks; ++i)
        s[(size_t) i] = engine.clips (i);
    return s;
}

void MixerWindow::restore (const Snapshot& s)
{
    for (int i = 0; i < mixer::maxTracks; ++i)
        engine.setClips (i, s[(size_t) i]);
    if (loopButton->isChecked())
        engine.setLoop (0, engine.length());
    lanes->clearSelection();
    rebuildStrips();
    changed();
}

void MixerWindow::remember()
{
    if (restoring)
        return;
    undoStack.push_back (snapshot());
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void MixerWindow::undo()
{
    if (undoStack.empty())
    {
        status->setText (tr ("Nothing to undo."));
        return;
    }
    redoStack.push_back (snapshot());
    restore (undoStack.back());
    undoStack.pop_back();
    status->setText (tr ("Undone (Ctrl+Shift+Z redoes it)."));
}

void MixerWindow::redo()
{
    if (redoStack.empty())
    {
        status->setText (tr ("Nothing to redo."));
        return;
    }
    undoStack.push_back (snapshot());
    restore (redoStack.back());
    redoStack.pop_back();
    status->setText (tr ("Redone."));
}

void MixerWindow::addFiles (int track, const QStringList& files, Frame at, bool movePlayhead)
{
    if (track < 0 || track >= engine.numTracks() || files.isEmpty())
        return;
    remember();
    placeFiles (track, files, std::max<Frame> (0, at), movePlayhead);
}

// One after another: each file starts where the one before it ends.
void MixerWindow::placeFiles (int track, QStringList files, Frame at, bool movePlayhead)
{
    if (files.isEmpty())
    {
        if (movePlayhead && ! engine.isPlaying())
        {
            engine.setPosition (at);
            refreshTransport();
            lanes->update();
        }
        return;
    }
    const auto file = files.takeFirst();
    mixer::Clip place;
    place.start = at;
    if (isMidiFile (file))
    {
        QString error;
        Frame end = at;
        if (! loadMidi (track, file, error, place, &end))
            QMessageBox::warning (this, tr ("MIDI File"), error);
        placeFiles (track, files, end, movePlayhead);
        return;
    }
    QPointer<MixerWindow> self (this);
    loadAudio (track, file, place, [self, track, files, movePlayhead] (Frame end)
    {
        if (self != nullptr)
            self->placeFiles (track, files, end, movePlayhead);
    });
}

void MixerWindow::stamp (mixer::Clips& list)
{
    for (auto& c : list)
        if (c.id == 0)
            c.id = nextClipId++;
}

void MixerWindow::loadAudio (int track, const QString& file, mixer::Clip place, std::function<void (Frame)> then)
{
    auto& t = info[(size_t) track];
    const auto generation = t.generation;
    const auto r = rate();
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());

    // A file already on a track at this rate is shared rather than read again.
    auto finish = [this, track, file, place, then] (mixer::AudioClipPtr clip) mutable
    {
        audioCache[file] = clip;
        place.audio = clip;
        if (place.length <= 0)
            place.length = clip->length();
        addClip (track, place);
        status->setText (tr ("%1 is on track %2 at %3").arg (QFileInfo (file).fileName()).arg (track + 1).arg (timeText ((double) place.start / rate())));
        if (then)
            then (place.end());
    };
    if (auto cached = audioCache[file].lock(); cached != nullptr)
    {
        finish (cached);
        return;
    }

    ++t.busy;
    lanes->update();
    status->setText (tr ("Loading %1...").arg (QFileInfo (file).fileName()));
    QPointer<MixerWindow> self (this);
    (void) QtConcurrent::run ([self, track, generation, file, r, finish]() mutable
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
        QMetaObject::invokeMethod (qApp, [self, track, generation, file, clip, error, finish]() mutable
        {
            if (self == nullptr || self->info[(size_t) track].generation != generation)
                return;
            auto& loading = self->info[(size_t) track];
            loading.busy = std::max (0, loading.busy - 1);
            if (clip == nullptr)
            {
                self->status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (error)));
                self->lanes->update();
                return;
            }
            finish (clip);
        }, Qt::QueuedConnection);
    });
}

bool MixerWindow::loadMidi (int track, const QString& file, QString& error, mixer::Clip place, Frame* end)
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
        for (const auto& c : engine.clips (i))
            first = first && ! c.isMidi();
    if (first && ! restoring && song->ticksPerQuarter > 0)
    {
        const QSignalBlocker block (tempoBox);
        tempoBox->setValue (song->fileBpm());
    }
    place.midi = mixer::placeMidi (song, file.toStdString(), rate(), followTempoBox->isChecked() ? tempoBox->value() : 0.0);
    if (place.reversed)
        place.midi = mixer::reverseMidi (*place.midi);
    if (place.length <= 0)
        place.length = place.midi->length;
    const auto length = place.length;
    if (end != nullptr)
        *end = place.end();
    addClip (track, place);
    // A MIDI file needs something to play it: the Synth until another instrument is chosen.
    if (engine.instrument (track) == nullptr && ! restoring)
        setInstrumentKind (track, synthChoice, {});
    status->setText (tr ("%1 is on track %2 at %3: %4 notes, %5 s").arg (QFileInfo (file).fileName()).arg (track + 1)
                         .arg (timeText ((double) place.start / rate())).arg (song->noteCount()).arg ((double) length / rate(), 0, 'f', 1));
    return true;
}

void MixerWindow::chooseAudio (int track)
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("Audio File for Track %1").arg (track + 1), folder(), audioFilter());
    if (! file.isEmpty())
        addFiles (track, { file }, engine.position());
    setFocus();
}

void MixerWindow::chooseMidi (int track)
{
    const auto file = QFileDialog::getOpenFileName (this, tr ("MIDI File for Track %1").arg (track + 1), folder(),
                                                    tr ("MIDI files (*.mid *.midi *.smf *.rmi);;All files (*)"));
    if (! file.isEmpty())
        addFiles (track, { file }, engine.position());
    setFocus();
}

void MixerWindow::placeAllMidi()
{
    // Placed again at the new tempo; a trimmed clip keeps the same part of the file.
    const auto bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        auto list = engine.clips (i);
        bool any = false;
        for (auto& c : list)
            if (c.midi != nullptr)
            {
                const auto before = std::max<Frame> (1, c.midi->length);
                const bool whole = c.offset == 0 && c.length == c.midi->length;
                c.midi = mixer::placeMidi (c.midi->song, c.midi->path, rate(), bpm);
                if (c.reversed)
                    c.midi = mixer::reverseMidi (*c.midi);
                const auto ratio = (double) c.midi->length / (double) before;
                c.offset = whole ? 0 : (Frame) std::llround ((double) c.offset * ratio);
                c.length = whole ? c.midi->length : (Frame) std::llround ((double) c.length * ratio);
                any = true;
            }
        if (any)
            engine.setClips (i, std::move (list));
    }
    if (loopButton->isChecked())
        engine.setLoop (0, engine.length());
    lanes->update();
    refreshTransport();
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
            case Qt::Key_Z: withShift ? redo() : undo(); return;
            case Qt::Key_B:
                if (! withShift)
                {
                    toggleBrowser();
                    return;
                }
                break;
            case Qt::Key_Y: redo(); return;
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
    appSettings().setValue (QStringLiteral ("mixer/sideSplitter"), sideSplitter->saveState());
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
        engine.setClips (i, {});
        engine.setInstrument (i, nullptr);
        auto& s = engine.strip (i);
        s.trimDb = 0.0f;
        s.faderDb = 0.0f;
        s.pan = 0.0f;
        s.invertLeft = s.invertRight = false;
        s.mute = s.solo = false;
        s.transpose = 0;
        s.channels = 0xFFFF;
        auto& t = info[(size_t) i];
        if (t.editor != nullptr)
            t.editor->close();
        t.name = tr ("Track %1").arg (i + 1);
        t.colour = trackColours[i];
        t.sound = nullptr;
        ++t.generation;
        t.busy = 0;
    }
    undoStack.clear();
    redoStack.clear();
    lanes->clearSelection();
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
            { "transpose", s.transpose.load() }, { "channels", (int) s.channels.load() },
            { "trim", s.trimDb.load() }, { "fader", s.faderDb.load() }, { "pan", s.pan.load() },
            { "invertLeft", s.invertLeft.load() }, { "invertRight", s.invertRight.load() },
            { "mute", s.mute.load() }, { "solo", s.solo.load() },
        };
        // The clips, in seconds so a mix opens the same at any rate.
        QJsonArray clips;
        for (const auto& c : engine.clips (i))
        {
            const auto source = c.audio != nullptr ? c.audio->path : c.midi->path;
            clips.append (QJsonObject {
                { "kind", c.isMidi() ? "midi" : "audio" }, { "file", storedPath (QString::fromStdString (source), base) },
                { "start", (double) c.start / rate() }, { "offset", (double) c.offset / rate() }, { "length", (double) c.length / rate() },
                { "muted", c.muted }, { "reversed", c.reversed },
            });
        }
        o.insert ("clips", clips);

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
        { "phinyxMix", 2 }, { "tempo", tempoBox->value() }, { "midiFollowsTempo", followTempoBox->isChecked() },
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

        // The clips; a mix from before clips has one audio and one MIDI file a track.
        const auto r = rate();
        auto frames = [r] (double seconds) { return (Frame) std::llround (seconds * r); };
        auto addClipFrom = [&] (const QString& clipKind, const QString& stored, mixer::Clip place)
        {
            const auto source = resolvedPath (stored, base);
            if (source.isEmpty())
                return;
            if (! QFileInfo::exists (source))
            {
                problems << tr ("Track %1: %2 is missing").arg (i + 1).arg (source);
                return;
            }
            if (clipKind == QLatin1String ("midi"))
            {
                QString why;
                if (! loadMidi (i, source, why, place))
                    problems << why;
            }
            else
                loadAudio (i, source, place);
        };
        if (o.contains (QStringLiteral ("clips")))
            for (const auto& value : o.value ("clips").toArray())
            {
                const auto c = value.toObject();
                mixer::Clip place;
                place.start = frames (c.value ("start").toDouble());
                place.offset = frames (c.value ("offset").toDouble());
                place.length = std::max<Frame> (1, frames (c.value ("length").toDouble()));
                place.muted = c.value ("muted").toBool();
                place.reversed = c.value ("reversed").toBool();
                addClipFrom (c.value ("kind").toString(), c.value ("file").toString(), place);
            }
        else
        {
            // A negative start was a file trimmed at its front.
            auto old = [&] (const char* key, const char* startKey, const char* onKey)
            {
                const auto at = frames (o.value (QLatin1String (startKey)).toDouble());
                mixer::Clip place;
                place.start = std::max<Frame> (0, at);
                place.offset = std::max<Frame> (0, -at);
                place.muted = ! o.value (QLatin1String (onKey)).toBool (true);
                return std::make_pair (o.value (QLatin1String (key)).toString(), place);
            };
            const auto [midiFile, midiPlace] = old ("midi", "midiStart", "midiOn");
            addClipFrom (QStringLiteral ("midi"), midiFile, midiPlace);
            const auto [audioFile, audioPlace] = old ("audio", "audioStart", "audioOn");
            addClipFrom (QStringLiteral ("audio"), audioFile, audioPlace);
        }
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
    if (QFileInfo (file).absoluteFilePath() != QFileInfo (autosavePath()).absoluteFilePath())   // the config folder is no place to look for files
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
// Bounce: the track's sound (its MIDI clips through its instrument, and its audio clips),
// before its fader and pan, from the start of the mix, written to a WAV and put back as a clip.
void MixerWindow::bounce (int track, bool inPlace)
{
    if (track < 0 || track >= engine.numTracks())
        return;
    if (engine.clips (track).empty())
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
    if (info[(size_t) track].busy > 0)
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

    remember();
    const auto clip = mixer::makeAudioClip (*result.data, file.toStdString());
    audioCache[file] = clip;
    auto& from = engine.strip (track);
    int target = track;
    if (inPlace)
    {
        // The clips stay, muted, so unmuting them undoes it (and so does Ctrl+Z); trim and
        // polarity are in the sound now.
        auto list = engine.clips (track);
        for (auto& c : list)
            c.muted = true;
        list.push_back (mixer::makeClip (clip, 0));   // the bounce starts at the start of the mix
        stamp (list);
        engine.setClips (track, std::move (list));
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
        to.transpose = 0;
        to.channels = 0xFFFF;
        engine.setInstrument (target, nullptr);
        mixer::Clips list { mixer::makeClip (clip, 0) };
        stamp (list);
        engine.setClips (target, std::move (list));
        info[(size_t) target].name = tr ("%1 bounce").arg (info[(size_t) track].name);
        info[(size_t) target].colour = info[(size_t) track].colour;
        from.mute = true;   // heard once, from the new track
    }
    if (loopButton->isChecked())
        engine.setLoop (0, engine.length());
    rebuildStrips();
    select (target);
    changed();
    status->setText (inPlace ? tr ("Bounced track %1 in place to %2 (its clips are kept, muted)").arg (track + 1).arg (file)
                             : tr ("Bounced track %1 to track %2 (%3); track %1 is muted").arg (track + 1).arg (target + 1).arg (file));
}

void MixerWindow::toggleLanes()
{
    lanes->setVisible (! lanes->isVisible());
    appSettings().setValue (QStringLiteral ("mixer/lanes"), lanes->isVisible());
    status->setText (lanes->isVisible() ? tr ("The tracks across time are shown above the strips (V hides them).")
                                        : tr ("The tracks across time are hidden (V shows them)."));
}

void MixerWindow::toggleBrowser()
{
    browser->setVisible (! browser->isVisible());
    appSettings().setValue (QStringLiteral ("mixer/browser"), browser->isVisible());
    if (browser->isVisible())
        browser->view()->setFocus();
    else
        setFocus();
}

// The sections in turn: the browser, the tracks across time, the strips (the window itself).
void MixerWindow::cycleFocus (bool forward)
{
    std::vector<QWidget*> order;
    if (browser->isVisible())
        order.push_back (browser->view());
    if (lanes->isVisible())
        order.push_back (lanes);
    order.push_back (this);
    auto* now = QApplication::focusWidget();
    int at = (int) order.size() - 1;
    for (int i = 0; i < (int) order.size(); ++i)
        if (now == order[(size_t) i] || (order[(size_t) i] != this && order[(size_t) i]->isAncestorOf (now)))
            at = i;
    if (now != nullptr && browser->isAncestorOf (now))
        at = 0;
    const auto next = (at + (forward ? 1 : -1) + (int) order.size()) % (int) order.size();
    order[(size_t) next]->setFocus (forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    status->setText (order[(size_t) next] == this ? tr ("The strips: 1-8 pick a track, Up and Down move its fader, M mutes, S solos.")
                   : order[(size_t) next] == lanes ? tr ("The tracks: Left and Right move the playhead, Up and Down pick a track, Delete removes the selected clips, Ctrl+E splits.")
                                                    : tr ("The browser: arrows pick a file, Enter puts it on the selected track at the playhead, Backspace goes up a folder."));
    if (lanes->isVisible())
        lanes->update();
}

bool MixerWindow::focusNextPrevChild (bool next)
{
    cycleFocus (next);
    return true;
}

//==============================================================================
// A clip edited in the editor: what it sounds like now, and the edit put back.
AudioDataPtr MixerWindow::clipAudio (int track, uint32_t id, QString& name) const
{
    if (track < 0 || track >= engine.numTracks())
        return nullptr;
    for (const auto& c : engine.clips (track))
    {
        if (c.id != id || c.audio == nullptr)
            continue;
        // The part heard, the way it plays.
        const auto& a = *c.audio;
        std::vector<float> left ((size_t) c.length), right ((size_t) c.length);
        for (Frame k = 0; k < c.length; ++k)
        {
            const auto forward = c.offset + k;
            const auto src = (size_t) (c.reversed ? a.length() - 1 - forward : forward);
            left[(size_t) k] = a.left[src];
            right[(size_t) k] = a.right[src];
        }
        AudioData::Builder b (a.mono ? 1 : 2, rate());
        const float* ptrs[2] = { left.data(), right.data() };
        b.append (ptrs, (int) c.length);
        name = tr ("Track %1 %2").arg (track + 1).arg (QFileInfo (QString::fromStdString (a.path)).completeBaseName());
        return b.finish();
    }
    return nullptr;
}

bool MixerWindow::replaceClipAudio (int track, uint32_t id, const AudioData& edited, const QString& name, QString& error)
{
    if (track < 0 || track >= engine.numTracks())
        return false;
    auto list = engine.clips (track);
    auto it = std::find_if (list.begin(), list.end(), [id] (const mixer::Clip& c) { return c.id == id; });
    if (it == list.end())
    {
        error = tr ("The clip is no longer on track %1.").arg (track + 1);
        return false;
    }

    // Saved beside the mix (in the Music folder until it is saved), the original left as it was.
    const auto base = path.isEmpty() ? QDir (QStandardPaths::writableLocation (QStandardPaths::MusicLocation)).filePath (tr ("PHINYX Edits"))
                                     : QFileInfo (path).absoluteDir().filePath (QFileInfo (path).completeBaseName() + tr (" Edits"));
    QDir().mkpath (base);
    auto stem = name;
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    auto file = QDir (base).filePath (stem + QStringLiteral (".wav"));
    for (int n = 2; QFileInfo::exists (file); ++n)
        file = QDir (base).filePath (QStringLiteral ("%1 %2.wav").arg (stem).arg (n));
    SaveOptions options;
    options.bitDepth = 32;
    std::string why;
    const AudioData* sound = &edited;
    AudioDataPtr resampled;
    if (std::abs (edited.sampleRate() - rate()) > 0.5)
    {
        resampled = dsp::resample (edited, rate());
        sound = resampled.get();
    }
    if (! AudioEngine::saveFile (file.toStdString(), *sound, {}, {}, options, why))
    {
        error = tr ("The edit could not be saved to %1: %2").arg (file, QString::fromStdString (why));
        return false;
    }

    remember();
    const auto clip = mixer::makeAudioClip (*sound, file.toStdString());
    audioCache[file] = clip;
    it->audio = clip;
    it->offset = 0;
    it->length = clip->length();
    it->reversed = false;   // the edit is the clip as it played
    editClips (track, std::move (list));
    status->setText (tr ("The edit is on track %1 (%2); Ctrl+Z puts the clip back as it was").arg (track + 1).arg (file));
    return true;
}
