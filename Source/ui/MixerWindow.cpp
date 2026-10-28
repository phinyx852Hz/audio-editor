// SPDX-License-Identifier: AGPL-3.0-only
#include "MixerWindow.h"
#include "ModeAtlas.h"
#include "../mixer/RagaInstrument.h"
#include "../core/PhiRaga.h"
#include "../mixer/DrumsToMidi.h"
#include "PhiMark.h"
#include "Potentiometer.h"
#include "SamplerWindow.h"
#include "ExtremeSettings.h"
#include "Settings.h"
#include "SlicerKit.h"
#include "SlicerWindow.h"
#include "Theme.h"
#include "TransportBar.h"
#include "../core/Dsp.h"
#include "../core/Phi.h"
#include "../mixer/Counterpoint.h"
#include "MidiEditor.h"
#include "../engine/AudioCapture.h"
#include "../engine/AudioEngine.h"
#include "../mixer/MtsEsp.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDial>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QPainterPath>
#include <QStackedWidget>
#include <QToolButton>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace
{
    constexpr int faderSteps = 1000;

    const char* const rootNames[12] { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };

    // A tuning choice in a mix: the system by its plain name, so the list can grow.
    QJsonValue tuningJson (const mixer::TuningChoice& c)
    {
        if (c.system < 0)
            return QJsonValue::Null;
        return QJsonObject { { "system", QString::fromStdString (mixer::tuningSystems()[(size_t) c.system].plain) }, { "root", c.root } };
    }

    mixer::TuningChoice tuningFromJson (const QJsonValue& v)
    {
        const auto o = v.toObject();
        mixer::TuningChoice c;
        c.system = mixer::findTuning (o.value ("system").toString().toStdString());
        c.root = std::clamp (o.value ("root").toInt(), 0, 11);
        return c;
    }

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

    QString hzText (float hz)
    {
        return hz >= 1000.0f ? QString::number (hz / 1000.0f, 'f', hz >= 10000.0f ? 0 : 1) + QStringLiteral ("k") : QString::number ((int) std::lround (hz));
    }

    // A strip's sections, each shown or hidden on every strip at once (the Strips menu).
    constexpr int knob = 30;   // a strip's pots

    enum StripSection { instrumentSection = 1, preSection = 2, insertSection = 4, panSection = 8, sendSection = 16 };
    constexpr int allSections = instrumentSection | preSection | insertSection | panSection | sendSection;

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

    // The Sample menu's Slicer has something to play: its sample, or pads with sounds of their own.
    bool slicerHasSound (const std::pair<sampler::SamplePtr, slicer::Kit>& s)
    {
        if (s.first != nullptr)
            return true;
        for (const auto& p : s.second.pads)
            for (const auto& l : p.layers)
                if (l.used && l.sound != nullptr)
                    return true;
        return false;
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

    QString configFile (const char* name)
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/mixer/") + QLatin1String (name);
    }

    QString projectFilter()
    {
        return MixerWindow::tr ("PHI projects (*.phi *.phinyxmix);;All files (*)");
    }

    enum Choice { noInstrument, synthChoice, samplerChoice, slicerChoice, ragaChoice, firstPlugin };
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
        // Zones to read from across the room: the theme's low colour up to -12 dB, its mid colour
        // to -6, between mid and high (orange) to -1, then its high colour.
        const auto yCaution = yFor (0.2512f), yMid = yFor (0.5f), yHigh = yFor (0.89f);   // -12, -6 and -1 dB
        const auto low = theme.color (QStringLiteral ("meterLow")), mid = theme.color (QStringLiteral ("meterMid"));
        const auto high = theme.color (QStringLiteral ("meterHigh"));
        const QColor orange ((mid.red() + high.red()) / 2, (mid.green() + high.green()) / 2, (mid.blue() + high.blue()) / 2);
        auto colourAt = [&] (int y) { return y <= yHigh ? high : y <= yMid ? orange : y <= yCaution ? mid : low; };
        for (int y = yPeak; y < top + height; ++y)
        {
            auto colour = colourAt (y);
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
// The mixer's messages: a line that shows for a few seconds, then gives its room back
// (owner, 2026-10-25: no standing message or MIDI list under the strips).
class StatusLine final : public QLabel
{
public:
    explicit StatusLine (QWidget* parent) : QLabel (parent)
    {
        setWordWrap (true);
        hide();
        timer.setSingleShot (true);
        QObject::connect (&timer, &QTimer::timeout, this, [this] { QLabel::clear(); hide(); });
    }

    std::function<void (const QString&)> echo;   // the same text elsewhere too

    void setText (const QString& text)
    {
        if (echo)
            echo (text);
        QLabel::setText (text);
        setVisible (! text.isEmpty());
        if (! text.isEmpty())
            timer.start (5000);
    }

private:
    QTimer timer;
};

class MixerStrip final : public QFrame
{
public:
    // `inspector`: the selected track's deep settings in the Inspector, without the fader.
    MixerStrip (MixerWindow& w, int i, bool inspector = false) : QFrame (&w), window (w), index (i), inspecting (inspector)
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
        // The name sits at the bottom, under the fader's value, in white on the track's colour
        // (owner, 2026-10-25); built here, placed after the fader below.
        nameBox = new QFrame (this);
        nameBox->setObjectName (QStringLiteral ("nameBox"));
        number = new QLabel (QString::number (index + 1), nameBox);
        nameEdit = new QLineEdit (nameBox);
        nameEdit->setFrame (false);
        nameEdit->setToolTip (MixerWindow::tr ("The track's name"));
        auto* head = new QHBoxLayout (nameBox);
        head->setContentsMargins (4, 1, 4, 1);
        head->addWidget (number);
        head->addWidget (nameEdit, 1);

        auto smallButton = [this] (const QString& text, const QString& tip, bool checkable)
        {
            auto* b = new QPushButton (text, this);
            b->setFocusPolicy (Qt::NoFocus);
            b->setCheckable (checkable);
            b->setToolTip (tip);
            b->setMinimumWidth (10);
            return b;
        };

        // The sections that can be hidden, each a widget of its own.
        auto section = [this, layout]
        {
            auto* part = new QWidget (this);
            auto* v = new QVBoxLayout (part);
            v->setContentsMargins (0, 0, 0, 0);
            v->setSpacing (3);
            layout->addWidget (part);
            return std::make_pair (part, v);
        };
        QVBoxLayout* sectionLayout = nullptr;

        // The track's mode: its audio clips, its instrument, or both as layers (Hybrid).
        modePart = new QWidget (this);
        auto* modeRow = new QHBoxLayout (modePart);
        modeRow->setContentsMargins (0, 0, 0, 0);
        modeRow->setSpacing (2);
        const char* modeNames[] = { QT_TRANSLATE_NOOP ("MixerWindow", "Hybrid"), QT_TRANSLATE_NOOP ("MixerWindow", "Audio"),
                                    QT_TRANSLATE_NOOP ("MixerWindow", "Inst") };
        const char* modeTips[] = {
            QT_TRANSLATE_NOOP ("MixerWindow", "Hybrid: the audio clips and the instrument together, as two layers with their own levels; one can duck the other"),
            QT_TRANSLATE_NOOP ("MixerWindow", "Audio: only the track's audio clips play"),
            QT_TRANSLATE_NOOP ("MixerWindow", "Instrument: only the instrument plays, from the MIDI clips and the keyboard"),
        };
        for (int m : { 1, 2, 0 })
        {
            auto* b = smallButton (MixerWindow::tr (modeNames[m]), MixerWindow::tr (modeTips[m]), true);
            auto f = b->font();
            f.setPointSizeF (std::max (6.5, f.pointSizeF() * 0.85));
            b->setFont (f);
            modeButtons[(size_t) m] = b;
            modeRow->addWidget (b);
            QObject::connect (b, &QPushButton::clicked, this, [this, m]
            {
                window.forLinked (index, [m] (mixer::Strip& x, bool) { x.mode = m; });
                refresh();
                applyLayout();
            });
        }
        layout->addWidget (modePart);

        std::tie (instrumentPart, sectionLayout) = section();

        // What the MIDI clips play.
        instrumentBox = new QComboBox (this);
        instrumentBox->setFocusPolicy (Qt::NoFocus);
        instrumentBox->setToolTip (MixerWindow::tr ("What the track's MIDI clips and the MIDI keyboard play: the built-in Synth (with drums on channel 10), "
                                                    "a Sampler or Slicer of the track's own, or an instrument plugin"));
        editButton = smallButton (MixerWindow::tr ("Edit"), MixerWindow::tr ("Open the instrument's window (E)"), false);
        auto* instRow = new QHBoxLayout;
        instRow->addWidget (instrumentBox, 1);
        instRow->addWidget (editButton);
        sectionLayout->addLayout (instRow);

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
        sectionLayout->addLayout (midiOptions);

        // Hybrid: the two layers' levels and the duck, filled in with the other pots below.
        QVBoxLayout* layerLayout = nullptr;
        std::tie (layerPart, layerLayout) = section();

        // Pre (Cubase): trim and polarity, the low and high cut, the track delay.
        std::tie (prePart, sectionLayout) = section();
        trimDial = new Potentiometer (this);
        trimDial->setRange (-240, 240);
        trimDial->setFixedSize (knob, knob);
        trimDial->setFocusPolicy (Qt::NoFocus);
        static_cast<Potentiometer*> (trimDial)->setOrigin (0);
        trimDial->setToolTip (MixerWindow::tr ("Trim, ±24 dB, before everything else (double-click: 0 dB)"));
        trimLabel = new QLabel (this);
        invertL = smallButton (QStringLiteral ("ØL"), MixerWindow::tr ("Flip the left side's polarity"), true);
        invertR = smallButton (QStringLiteral ("ØR"), MixerWindow::tr ("Flip the right side's polarity"), true);
        // Trim sits with pan and width, smaller pots side by side with their values below.
        lowCutButton = smallButton ({}, MixerWindow::tr ("Low cut: click turns it on or off; right-click for its frequency and slope"), true);
        highCutButton = smallButton ({}, MixerWindow::tr ("High cut: click turns it on or off; right-click for its frequency and slope"), true);
        auto* pre = new QHBoxLayout;
        pre->setSpacing (2);
        pre->addWidget (invertL);
        pre->addWidget (invertR);
        pre->addWidget (lowCutButton);
        pre->addWidget (highCutButton);
        sectionLayout->addLayout (pre);
        delayBox = new QDoubleSpinBox (this);
        delayBox->setRange (-mixer::maxDelayMs, mixer::maxDelayMs);
        delayBox->setDecimals (1);
        delayBox->setSingleStep (1.0);
        delayBox->setPrefix (MixerWindow::tr ("Delay "));
        delayBox->setSuffix (MixerWindow::tr (" ms"));
        delayBox->setToolTip (MixerWindow::tr ("Track delay: the track's clips play this much later (or, below 0, earlier) than the others, "
                                               "to line them up or for groove"));
        sectionLayout->addWidget (delayBox);

        // Inserts: effects before the width, pan and fader.
        std::tie (insertPart, sectionLayout) = section();
        insertTitle = new QLabel (MixerWindow::tr ("Inserts"), this);
        insertTitle->setEnabled (false);
        addInsertButton = smallButton (QStringLiteral ("+"), MixerWindow::tr ("Add an effect to the track's inserts (Insert)"), false);
        addInsertButton->setMaximumWidth (26);
        auto* insertHead = new QHBoxLayout;
        insertHead->addWidget (insertTitle, 1);
        insertHead->addWidget (addInsertButton);
        sectionLayout->addLayout (insertHead);
        insertList = new QListWidget (this);
        insertList->setFocusPolicy (Qt::NoFocus);
        insertList->setContextMenuPolicy (Qt::CustomContextMenu);
        insertList->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
        insertList->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
        insertList->setDragDropMode (QAbstractItemView::InternalMove);
        insertList->setToolTip (MixerWindow::tr ("The track's insert slots, top first. Click an empty one to add an effect; double-click one to open it; "
                                                 "right-click to bypass, move or remove it; drag to reorder"));
        sectionLayout->addWidget (insertList);

        // Sends (Ableton): A and B, filled in below with the other pots.
        QVBoxLayout* sendLayout = nullptr;
        std::tie (sendPart, sendLayout) = section();

        std::tie (panPart, sectionLayout) = section();
        panDial = new Potentiometer (this);
        panDial->setRange (-100, 100);
        panDial->setFixedSize (knob, knob);
        panDial->setFocusPolicy (Qt::NoFocus);
        static_cast<Potentiometer*> (panDial)->setOrigin (0);
        panDial->setToolTip (MixerWindow::tr ("Pan ([ and ]): equal power for a mono file, balance for stereo and instruments (double-click: centre)"));
        panLabel = new QLabel (this);
        auto knobColumn = [this] (QDial* dial, QLabel* label, const QString& title)
        {
            auto* part = new QWidget (this);
            auto* column = new QVBoxLayout (part);
            column->setContentsMargins (0, 0, 0, 0);
            column->setSpacing (0);
            auto* name = new QLabel (title, part);
            name->setEnabled (false);
            auto f = name->font();
            f.setPointSizeF (std::max (6.5, f.pointSizeF() * 0.8));
            name->setFont (f);
            label->setFont (f);
            column->addWidget (name, 0, Qt::AlignHCenter);
            column->addWidget (dial, 0, Qt::AlignHCenter);
            column->addWidget (label, 0, Qt::AlignHCenter);
            return part;
        };
        trimPart = knobColumn (trimDial, trimLabel, MixerWindow::tr ("Trim"));
        panColumnPart = knobColumn (panDial, panLabel, MixerWindow::tr ("Pan"));
        widthDial = new Potentiometer (this);
        widthDial->setRange (0, 200);
        widthDial->setFixedSize (knob, knob);
        widthDial->setFocusPolicy (Qt::NoFocus);
        static_cast<Potentiometer*> (widthDial)->setOrigin (100);
        widthDial->setToolTip (MixerWindow::tr ("Width, after the inserts: 0 % mono, 100 % as it is, 200 % twice as wide (double-click: 100 %). "
                                                "A track of mono files has none"));
        widthLabel = new QLabel (this);
        widthPart = knobColumn (widthDial, widthLabel, MixerWindow::tr ("Width"));
        auto* sendRow = new QHBoxLayout;
        sendRow->setSpacing (2);
        for (int r = 0; r < mixer::numReturns; ++r)
        {
            auto* dial = new Potentiometer (this);
            dial->setRange (0, faderPosition (0.0));
            dial->setFixedSize (knob, knob);
            dial->setFocusPolicy (Qt::NoFocus);
            dial->setContextMenuPolicy (Qt::CustomContextMenu);
            dial->setToolTip (MixerWindow::tr ("Send %1 to the %2 return, -∞ to 0 dB, after the fader (right-click: before it; double-click: -∞)")
                                  .arg (QChar ('A' + r)).arg (r == 0 ? MixerWindow::tr ("A Reverb") : MixerWindow::tr ("B Delay")));
            sendDials[(size_t) r] = dial;
            sendLabels[(size_t) r] = new QLabel (this);
            sendRow->addWidget (knobColumn (dial, sendLabels[(size_t) r], r == 0 ? MixerWindow::tr ("Send A") : MixerWindow::tr ("Send B")));
            dial->installEventFilter (this);
            QObject::connect (dial, &QDial::valueChanged, this, [this, r] (int v)
            {
                if (updating)
                    return;
                const auto db = faderDb (v);
                const auto delta = db - strip().sendDb[(size_t) r].load();
                window.forLinked (index, [r, db, delta] (mixer::Strip& s, bool self)
                {
                    s.sendDb[(size_t) r] = self ? db : (float) std::clamp ((double) std::max (-60.0f, s.sendDb[(size_t) r].load()) + delta, -100.0, 0.0);
                });
                showValues();
            });
            QObject::connect (dial, &QWidget::customContextMenuRequested, this, [this, r]
            {
                QMenu menu (this);
                auto* preAction = menu.addAction (MixerWindow::tr ("Pre-Fader"));
                preAction->setCheckable (true);
                preAction->setChecked (strip().sendPre[(size_t) r].load());
                QObject::connect (preAction, &QAction::toggled, this, [this, r] (bool on)
                {
                    window.forLinked (index, [r, on] (mixer::Strip& s, bool) { s.sendPre[(size_t) r] = on; });
                });
                menu.exec (QCursor::pos());
            });
        }
        sendLayout->addLayout (sendRow);

        auto layerDial = [this] (int minimum, int maximum, const QString& tip)
        {
            auto* dial = new Potentiometer (this);
            dial->setRange (minimum, maximum);
            dial->setFixedSize (knob, knob);
            dial->setFocusPolicy (Qt::NoFocus);
            dial->setToolTip (tip);
            dial->installEventFilter (this);
            return dial;
        };
        audioLayerDial = layerDial (0, faderSteps, MixerWindow::tr ("The audio clips' level in the hybrid track (double-click: 0 dB)"));
        instLayerDial = layerDial (0, faderSteps, MixerWindow::tr ("The instrument's level in the hybrid track (double-click: 0 dB)"));
        duckDial = layerDial (0, 480, MixerWindow::tr ("Duck: the key (the instrument, or another track) pushes this track down by up to this much "
                                                       "as it plays (a kick pumping a loop); right-click for the key, Hard, Soft and HF-safe, "
                                                       "the band, the attack and the release"));
        duckDial->setContextMenuPolicy (Qt::CustomContextMenu);
        audioLayerLabel = new QLabel (this);
        instLayerLabel = new QLabel (this);
        duckLabel = new QLabel (this);
        auto* layerRow = new QHBoxLayout;
        layerRow->setSpacing (2);
        audioLayerPart = knobColumn (audioLayerDial, audioLayerLabel, MixerWindow::tr ("Audio"));
        instLayerPart = knobColumn (instLayerDial, instLayerLabel, MixerWindow::tr ("Inst"));
        layerRow->addWidget (audioLayerPart);
        layerRow->addWidget (instLayerPart);
        layerRow->addWidget (knobColumn (duckDial, duckLabel, MixerWindow::tr ("Duck")));
        layerLayout->addLayout (layerRow);
        QObject::connect (audioLayerDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto db = faderDb (v);
            window.forLinked (index, [db] (mixer::Strip& x, bool) { x.audioLayerDb = db; });
            showValues();
        });
        QObject::connect (instLayerDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto db = faderDb (v);
            window.forLinked (index, [db] (mixer::Strip& x, bool) { x.instrumentLayerDb = db; });
            showValues();
        });
        QObject::connect (duckDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto db = (float) v / 10.0f;
            window.forLinked (index, [db] (mixer::Strip& x, bool) { x.duckDb = db; });
            showValues();
        });
        QObject::connect (duckDial, &QWidget::customContextMenuRequested, this, [this] { duckMenu(); });

        auto* panRow = new QHBoxLayout;
        panRow->setSpacing (2);
        panRow->addWidget (trimPart);
        panRow->addWidget (panColumnPart);
        panRow->addWidget (widthPart);
        sectionLayout->addLayout (panRow);

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
        layout->addWidget (nameBox);
        if (inspecting)
        {
            // The Inspector: its name on top, no fader (the strip has it).
            layout->removeWidget (nameBox);
            layout->insertWidget (1, nameBox);
            for (QWidget* x : std::initializer_list<QWidget*> { muteButton, soloButton, fader, meter, faderValue, peakValue })
                x->hide();
            layout->addStretch (1);
            setFrameShape (QFrame::NoFrame);
        }

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
        // Q-Link (or Alt+Shift, every track): a change to one strip goes to the others too,
        // levels moved by the same amount, switches set the same.
        QObject::connect (trimDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto db = (float) v / 10.0f, delta = db - strip().trimDb.load();
            window.forLinked (index, [db, delta] (mixer::Strip& s, bool self) { s.trimDb = self ? db : std::clamp (s.trimDb.load() + delta, -24.0f, 24.0f); });
            showValues();
        });
        QObject::connect (invertL, &QPushButton::toggled, this, [this] (bool on)
        {
            if (! updating)
                window.forLinked (index, [on] (mixer::Strip& s, bool) { s.invertLeft = on; });
        });
        QObject::connect (invertR, &QPushButton::toggled, this, [this] (bool on)
        {
            if (! updating)
                window.forLinked (index, [on] (mixer::Strip& s, bool) { s.invertRight = on; });
        });
        QObject::connect (panDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto pan = (float) v / 100.0f;
            window.forLinked (index, [pan] (mixer::Strip& s, bool) { s.pan = pan; });
            showValues();
        });
        QObject::connect (widthDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto width = (float) v / 100.0f;
            window.forLinked (index, [width] (mixer::Strip& s, bool) { s.width = width; });
            showValues();
        });
        QObject::connect (lowCutButton, &QPushButton::toggled, this, [this] (bool on)
        {
            if (! updating)
                window.forLinked (index, [on] (mixer::Strip& s, bool) { s.lowCut = on; });
        });
        QObject::connect (highCutButton, &QPushButton::toggled, this, [this] (bool on)
        {
            if (! updating)
                window.forLinked (index, [on] (mixer::Strip& s, bool) { s.highCut = on; });
        });
        lowCutButton->setContextMenuPolicy (Qt::CustomContextMenu);
        highCutButton->setContextMenuPolicy (Qt::CustomContextMenu);
        QObject::connect (lowCutButton, &QWidget::customContextMenuRequested, this, [this] { cutMenu (false); });
        QObject::connect (highCutButton, &QWidget::customContextMenuRequested, this, [this] { cutMenu (true); });
        QObject::connect (delayBox, &QDoubleSpinBox::valueChanged, this, [this] (double value)
        {
            if (updating)
                return;
            window.forLinked (index, [value] (mixer::Strip& s, bool) { s.delayMs = (float) value; });
        });
        QObject::connect (delayBox, &QDoubleSpinBox::editingFinished, &window, [this] { window.setFocus(); });
        QObject::connect (addInsertButton, &QPushButton::clicked, this, [this]
        {
            window.select (index);
            window.chooseInsert (index, -1, addInsertButton->mapToGlobal (QPoint (0, addInsertButton->height())));
        });
        QObject::connect (insertList, &QListWidget::itemClicked, this, [this] (QListWidgetItem* item)
        {
            window.select (index);
            if (item->data (Qt::UserRole).toInt() < 0)
                window.chooseInsert (index, -1, insertList->viewport()->mapToGlobal (insertList->visualItemRect (item).bottomLeft()));
        });
        QObject::connect (insertList, &QListWidget::itemDoubleClicked, this, [this] (QListWidgetItem* item)
        {
            window.select (index);
            window.editInsert (index, item->data (Qt::UserRole).toInt());
        });
        QObject::connect (insertList, &QWidget::customContextMenuRequested, this, [this] (QPoint at)
        {
            window.select (index);
            const auto* item = insertList->itemAt (at);
            window.insertMenu (index, item != nullptr ? item->data (Qt::UserRole).toInt() : -1, insertList->viewport()->mapToGlobal (at));
        });
        // A drag in the list reorders the inserts.
        QObject::connect (insertList->model(), &QAbstractItemModel::rowsMoved, this, [this]
        {
            if (updating)
                return;
            std::vector<int> order;
            for (int k = 0; k < insertList->count(); ++k)
                if (const auto at = insertList->item (k)->data (Qt::UserRole).toInt(); at >= 0)
                    order.push_back (at);
            // Refreshed later: the list is still in the middle of its drop.
            QTimer::singleShot (0, &window, [this, order] { window.reorderInserts (index, order); });
        });
        QObject::connect (muteButton, &QPushButton::toggled, this, [this] (bool on)
        {
            if (! updating)
                window.forLinked (index, [on] (mixer::Strip& s, bool) { s.mute = on; });
        });
        QObject::connect (soloButton, &QPushButton::toggled, this, [this] (bool on)
        {
            if (! updating)
                window.forLinked (index, [on] (mixer::Strip& s, bool) { s.solo = on; });
        });
        QObject::connect (fader, &QSlider::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            const auto db = faderDb (v);
            const auto delta = db - std::max (-60.0f, strip().faderDb.load());
            window.forLinked (index, [db, delta] (mixer::Strip& s, bool self)
            {
                s.faderDb = self ? db : (float) std::clamp ((double) std::max (-60.0f, s.faderDb.load()) + delta, -100.0, 6.0);
            });
            showValues();
        });
        trimDial->installEventFilter (this);
        panDial->installEventFilter (this);
        widthDial->installEventFilter (this);
        applyLayout();
        faderValue->installEventFilter (this);
    }

    mixer::Strip& strip() { return window.engine.strip (index); }

    // The instrument list (built-ins, then the instrument plugins found by the scan) with the
    // track's instrument chosen; also the launcher's rack.
    static mixer::InstrumentPtr fillInstruments (QComboBox* box, MixerWindow& window, int index)
    {
        const auto plugins = window.audio.knownInstruments();
        if (box->count() != firstPlugin + (int) plugins.size())
        {
            box->clear();
            box->addItems ({ MixerWindow::tr ("No instrument"), MixerWindow::tr ("Synth"), MixerWindow::tr ("Sampler"), MixerWindow::tr ("Slicer"),
                             QString::fromUtf8 ("PHI rāga") });
            for (const auto& p : plugins)
                box->addItem (QString::fromStdString (p.name) + QStringLiteral (" (") + QString::fromStdString (p.format) + QStringLiteral (")"),
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
                case mixer::Instrument::Kind::raga:    choice = ragaChoice; break;
                case mixer::Instrument::Kind::plugin:
                {
                    choice = box->findData (QString::fromStdString (AudioEngine::instrumentPluginId (*inst)));
                    if (choice < 0)
                    {
                        box->addItem (QString::fromStdString (inst->name()), QString::fromStdString (AudioEngine::instrumentPluginId (*inst)));
                        choice = box->count() - 1;
                    }
                    break;
                }
            }
        box->setCurrentIndex (choice);
        return inst;
    }

    // The launcher's strips (owner, 2026-10-25): four insert slots as tall as the launcher's,
    // the sends above trim and pan, mute and solo, a shorter fader and meter, and the name.
    // The rest is in the Inspector.
    void setCompact (bool on)
    {
        compact = on;
        applyLayout();
    }

    // The sections shown (the Strips menu, or the launcher's compact strips) and the width.
    void applyLayout()
    {
        const auto shown = window.stripSections;
        const auto mode = (mixer::TrackMode) strip().mode.load();
        if (inspecting)
        {
            // Everything the strips leave out; the Strips menu picks the sections.
            modePart->setVisible (true);
            instrumentPart->setVisible ((shown & instrumentSection) != 0 && mode != mixer::TrackMode::audio);
            layerPart->setVisible ((shown & instrumentSection) != 0);
            audioLayerPart->setVisible (mode == mixer::TrackMode::hybrid);
            instLayerPart->setVisible (mode == mixer::TrackMode::hybrid);
            prePart->setVisible ((shown & preSection) != 0);
            insertPart->setVisible ((shown & insertSection) != 0);
            insertTitle->setVisible (true);
            addInsertButton->setVisible (true);
            sendPart->setVisible ((shown & sendSection) != 0);
            panPart->setVisible ((shown & panSection) != 0);
            trimPart->setVisible (false);
            panColumnPart->setVisible (false);
            widthPart->setVisible (true);
            refreshInserts();
            setMinimumWidth (180);
            setMaximumWidth (QWIDGETSIZE_MAX);
            return;
        }
        instrumentPart->setVisible (! compact && (shown & instrumentSection) != 0 && mode != mixer::TrackMode::audio);
        // The duck in every mode (another track can key it); the layer levels only in a hybrid.
        layerPart->setVisible (! compact && (shown & instrumentSection) != 0);
        audioLayerPart->setVisible (mode == mixer::TrackMode::hybrid);
        instLayerPart->setVisible (mode == mixer::TrackMode::hybrid);
        prePart->setVisible (! compact && (shown & preSection) != 0);
        modePart->setVisible (! compact);
        insertPart->setVisible (compact || (shown & insertSection) != 0);
        insertTitle->setVisible (! compact);
        addInsertButton->setVisible (! compact);
        sendPart->setVisible (compact || (shown & sendSection) != 0);
        panPart->setVisible ((shown & panSection) != 0 || compact);
        widthPart->setVisible (! compact);
        trimPart->setVisible (true);
        fader->setMinimumHeight (compact ? compactFader : 0);
        refreshInserts();
        const bool wide = window.wideStrips && ! compact;
        setMinimumWidth (compact ? 96 : wide ? 180 : 128);
        setMaximumWidth (compact ? 150 : wide ? 240 : 170);
    }

    // The inserts as the engine has them; each row keeps its place in the engine's list.
    void refreshInserts()
    {
        const auto list = window.engine.inserts (index);
        const bool was = updating;
        updating = true;
        insertList->clear();
        // Slots as on a Cubase strip, always there: at least four, one empty after the last used
        // (in the launcher just the four, unless more are used).
        const auto slots = std::min (mixer::maxInserts, std::max (4, (int) list.size() + (compact ? 0 : 1)));
        for (int k = (int) list.size(); k < slots; ++k)
        {
            auto* item = new QListWidgetItem (QStringLiteral ("%1  —").arg (k + 1));
            item->setData (Qt::UserRole, -1);
            item->setForeground (palette().color (QPalette::Disabled, QPalette::Text));
            item->setToolTip (MixerWindow::tr ("An empty insert slot: click it to add an effect"));
            item->setFlags (item->flags() & ~Qt::ItemIsDragEnabled);
            insertList->addItem (item);
        }
        for (size_t k = 0; k < list.size(); ++k)
        {
            // A parallel insert shows how much of it is heard.
            const auto mix = (int) std::lround (list[k].mix * 100.0f);
            auto* item = new QListWidgetItem (QStringLiteral ("%1  ").arg (k + 1) + QString::fromStdString (list[k].effect->name())
                                              + (mix < 100 ? QStringLiteral (" %1%").arg (mix) : QString()));
            insertList->insertItem ((int) k, item);
            item->setData (Qt::UserRole, (int) k);
            item->setToolTip (list[k].bypassed ? MixerWindow::tr ("%1 (bypassed)").arg (item->text()) : item->text());
            if (list[k].bypassed)
            {
                auto f = item->font();
                f.setStrikeOut (true);
                item->setFont (f);
                item->setForeground (palette().color (QPalette::Disabled, QPalette::Text));
            }
        }
        // As tall as its slots, no scrolling; in the launcher, as tall as its clip slots.
        const auto row = compact ? compactSlot : std::max (16, insertList->fontMetrics().height() + 4);
        for (int k = 0; k < insertList->count(); ++k)
            insertList->item (k)->setSizeHint (QSize (0, row));
        insertList->setFixedHeight (row * slots + 2 * insertList->frameWidth() + 2);
        addInsertButton->setEnabled ((int) list.size() < mixer::maxInserts);
        updating = was;
    }

    // What keys the duck (one layer of a hybrid, or another track), how hard, which band, how fast.
    void duckMenu()
    {
        window.select (index);
        const auto& s = strip();
        const auto key = s.duckKey.load();
        QMenu menu (this);
        auto* keys = new QActionGroup (&menu);
        auto keyAction = [&] (const QString& text, int track, bool audioLeads)
        {
            auto* a = menu.addAction (text);
            a->setCheckable (true);
            a->setActionGroup (keys);
            a->setChecked (key == track && (track >= 0 || s.audioDucksInstrument.load() == audioLeads));
            QObject::connect (a, &QAction::triggered, this, [this, track, audioLeads]
            {
                window.forLinked (index, [track, audioLeads] (mixer::Strip& x, bool) { x.duckKey = track; x.audioDucksInstrument = audioLeads; });
                refresh();
            });
            return a;
        };
        const bool hybrid = s.mode.load() == (int) mixer::TrackMode::hybrid;
        keyAction (MixerWindow::tr ("The Instrument Ducks the Audio"), -1, false)->setEnabled (hybrid);
        keyAction (MixerWindow::tr ("The Audio Ducks the Instrument"), -1, true)->setEnabled (hybrid);
        auto* from = menu.addMenu (MixerWindow::tr ("Ducked by Another &Track"));
        for (int t = 0; t < window.engine.numTracks(); ++t)
            if (t != index)
            {
                auto* a = keyAction (window.trackName (t), t, false);
                menu.removeAction (a);
                from->addAction (a);
            }
        menu.addSeparator();
        // The three ducking groups: a hard pump, a gentle one, and one that leaves the highs alone.
        struct Preset { const char* name; float db, attack, release, below; };
        static const Preset presets[] = {
            { QT_TRANSLATE_NOOP ("MixerWindow", "Hard: 24 dB, Fast Release"), 24.0f, 1.0f, 120.0f, 0.0f },
            { QT_TRANSLATE_NOOP ("MixerWindow", "Soft: 8 dB, Slow Release"), 8.0f, 5.0f, 220.0f, 0.0f },
            { QT_TRANSLATE_NOOP ("MixerWindow", "HF-Safe: 12 dB Below 150 Hz"), 12.0f, 1.0f, 150.0f, 150.0f },
        };
        for (const auto& p : presets)
            menu.addAction (MixerWindow::tr (p.name), this, [this, p]
            {
                window.forLinked (index, [p] (mixer::Strip& x, bool)
                {
                    x.duckDb = p.db;
                    x.duckAttackMs = p.attack;
                    x.duckReleaseMs = p.release;
                    x.duckBelowHz = p.below;
                });
                refresh();
            });
        menu.addSeparator();
        const auto below = s.duckBelowHz.load();
        menu.addAction (below > 0.0f ? MixerWindow::tr ("Only Below: %1 Hz...").arg (hzText (below)) : MixerWindow::tr ("Only Below: Full Band..."), this, [this, below]
        {
            bool ok = false;
            const auto v = QInputDialog::getInt (this, MixerWindow::tr ("Duck"), MixerWindow::tr ("Duck only what lies below (Hz; 0: the whole sound):"),
                                                 (int) std::lround (below), 0, 2000, 10, &ok);
            if (ok)
                window.forLinked (index, [v] (mixer::Strip& x, bool) { x.duckBelowHz = (float) v; });
            showValues();
        });
        auto timing = [this] (const QString& title, std::atomic<float> mixer::Strip::* field, double low, double high)
        {
            bool ok = false;
            const auto v = QInputDialog::getDouble (this, MixerWindow::tr ("Duck"), title, (strip().*field).load(), low, high, 1, &ok);
            if (ok)
                window.forLinked (index, [field, v] (mixer::Strip& x, bool) { (x.*field) = (float) v; });
        };
        menu.addAction (MixerWindow::tr ("Attack: %1 ms...").arg (s.duckAttackMs.load(), 0, 'f', 1), this, [timing]
        {
            timing (MixerWindow::tr ("Attack (ms):"), &mixer::Strip::duckAttackMs, 0.1, 100.0);
        });
        menu.addAction (MixerWindow::tr ("Release: %1 ms...").arg (s.duckReleaseMs.load(), 0, 'f', 0), this, [timing]
        {
            timing (MixerWindow::tr ("Release (ms):"), &mixer::Strip::duckReleaseMs, 5.0, 2000.0);
        });
        menu.exec (QCursor::pos());
    }

    // The low or high cut's frequency and slope (for the linked strips too).
    void cutMenu (bool high)
    {
        window.select (index);
        const auto& s = strip();
        const auto on = (high ? s.highCut : s.lowCut).load();
        const auto hz = (high ? s.highCutHz : s.lowCutHz).load();
        const auto slope = (high ? s.highCutSlope : s.lowCutSlope).load();
        auto setCut = [this, high] (std::optional<bool> turnOn, std::optional<float> frequency, std::optional<int> dbPerOctave)
        {
            window.forLinked (index, [=] (mixer::Strip& x, bool)
            {
                if (turnOn)
                    (high ? x.highCut : x.lowCut) = *turnOn;
                if (frequency)
                    (high ? x.highCutHz : x.lowCutHz) = *frequency;
                if (dbPerOctave)
                    (high ? x.highCutSlope : x.lowCutSlope) = *dbPerOctave;
            });
            refresh();
        };
        QMenu menu (this);
        auto* onAction = menu.addAction (high ? MixerWindow::tr ("High Cut On") : MixerWindow::tr ("Low Cut On"));
        onAction->setCheckable (true);
        onAction->setChecked (on);
        QObject::connect (onAction, &QAction::toggled, this, [setCut] (bool v) { setCut (v, {}, {}); });
        menu.addSeparator();
        const std::vector<float> choices = high ? std::vector<float> { 5000, 6000, 8000, 10000, 12000, 15000, 18000 }
                                                : std::vector<float> { 30, 40, 60, 70, 80, 100, 120, 150, 200, 300 };
        for (const auto f : choices)
        {
            auto* a = menu.addAction (hzText (f) + MixerWindow::tr (" Hz"));
            a->setCheckable (true);
            a->setChecked (std::abs (hz - f) < 0.5f);
            QObject::connect (a, &QAction::triggered, this, [setCut, f] { setCut (true, f, {}); });
        }
        menu.addAction (MixerWindow::tr ("Frequency..."), this, [this, setCut, hz, high]
        {
            bool ok = false;
            const auto f = QInputDialog::getDouble (this, high ? MixerWindow::tr ("High Cut") : MixerWindow::tr ("Low Cut"), MixerWindow::tr ("Frequency (Hz):"),
                                                    hz, mixer::minCutHz, mixer::maxCutHz, 0, &ok);
            if (ok)
                setCut (true, (float) f, {});
        });
        menu.addSeparator();
        for (const int db : { 12, 24 })
        {
            auto* a = menu.addAction (MixerWindow::tr ("%1 dB an Octave").arg (db));
            a->setCheckable (true);
            a->setChecked (slope == db);
            QObject::connect (a, &QAction::triggered, this, [setCut, db] { setCut ({}, {}, db); });
        }
        auto* button = high ? highCutButton : lowCutButton;
        menu.exec (button->mapToGlobal (QPoint (0, button->height())));
    }

    // Everything shown, from the engine and the window's track info.
    void refresh()
    {
        updating = true;
        auto& s = strip();
        const auto& info = window.info[(size_t) index];
        colourBar->setStyleSheet (QStringLiteral ("background: %1;").arg (info.colour.name()));
        nameBox->setStyleSheet (QStringLiteral ("QFrame#nameBox { background: %1; border-radius: 3px; } "
                                                "QFrame#nameBox QLabel, QFrame#nameBox QLineEdit { color: white; background: transparent; }")
                                    .arg (info.colour.name()));
        if (nameEdit->text() != info.name)
            nameEdit->setText (info.name);

        const auto inst = fillInstruments (instrumentBox, window, index);
        editButton->setEnabled (inst != nullptr && (inst->kind() != mixer::Instrument::Kind::plugin || inst->hasWindow()));

        transposeBox->setValue (s.transpose.load());
        armButton->setChecked (window.engine.liveTrack() == index);
        trimDial->setValue ((int) std::lround (s.trimDb.load() * 10.0f));
        invertL->setChecked (s.invertLeft.load());
        invertR->setChecked (s.invertRight.load());
        panDial->setValue ((int) std::lround (s.pan.load() * 100.0f));
        widthDial->setValue ((int) std::lround (s.width.load() * 100.0f));
        const bool mono = window.engine.monoTrack (index);
        widthDial->setEnabled (! mono);
        widthLabel->setEnabled (! mono);
        lowCutButton->setChecked (s.lowCut.load());
        highCutButton->setChecked (s.highCut.load());
        delayBox->setValue (s.delayMs.load());
        for (int m = 0; m < 3; ++m)
            modeButtons[(size_t) m]->setChecked (s.mode.load() == m);
        audioLayerDial->setValue (faderPosition (s.audioLayerDb.load()));
        instLayerDial->setValue (faderPosition (s.instrumentLayerDb.load()));
        duckDial->setValue ((int) std::lround (s.duckDb.load() * 10.0f));
        for (int r = 0; r < mixer::numReturns; ++r)
            sendDials[(size_t) r]->setValue (faderPosition (s.sendDb[(size_t) r].load()));
        refreshInserts();
        muteButton->setChecked (s.mute.load());
        soloButton->setChecked (s.solo.load());
        fader->setValue (faderPosition (s.faderDb.load()));
        updating = false;
        showValues();
    }

    void showValues()
    {
        auto& s = strip();
        trimLabel->setText (dbText (s.trimDb.load()));
        panLabel->setText (panText (s.pan.load()));
        widthLabel->setText (window.engine.monoTrack (index) ? MixerWindow::tr ("mono") : QStringLiteral ("%1%").arg ((int) std::lround (s.width.load() * 100.0f)));
        const auto slope = [] (int db) { return db >= 24 ? QStringLiteral ("²") : QString(); };
        // Narrow strips name the cuts; wide ones give their frequency too (it is in the tooltip anyway).
        const bool wide = window.wideStrips;
        lowCutButton->setText (wide ? MixerWindow::tr ("LC %1").arg (hzText (s.lowCutHz.load())) + slope (s.lowCutSlope.load()) : MixerWindow::tr ("LC"));
        highCutButton->setText (wide ? MixerWindow::tr ("HC %1").arg (hzText (s.highCutHz.load())) + slope (s.highCutSlope.load()) : MixerWindow::tr ("HC"));
        lowCutButton->setToolTip (MixerWindow::tr ("Low cut at %1 Hz, %2 dB an octave: click turns it on or off; right-click for its frequency and slope")
                                      .arg (hzText (s.lowCutHz.load())).arg (s.lowCutSlope.load()));
        highCutButton->setToolTip (MixerWindow::tr ("High cut at %1 Hz, %2 dB an octave: click turns it on or off; right-click for its frequency and slope")
                                       .arg (hzText (s.highCutHz.load())).arg (s.highCutSlope.load()));
        audioLayerLabel->setText (dbText (s.audioLayerDb.load()));
        instLayerLabel->setText (dbText (s.instrumentLayerDb.load()));
        // Who keys it (I, A or a track number) and how deep; a band duck is marked with a ↓.
        const auto key = s.duckKey.load();
        const bool keyed = key >= 0 || s.mode.load() == (int) mixer::TrackMode::hybrid;
        const auto keyText = key >= 0 ? QString::number (key + 1) : s.audioDucksInstrument.load() ? QStringLiteral ("A") : QStringLiteral ("I");
        duckLabel->setText (s.duckDb.load() < 0.05f || ! keyed ? MixerWindow::tr ("off")
                                                               : QStringLiteral ("%1→%2%3").arg (keyText).arg (s.duckDb.load(), 0, 'f', 0)
                                                                     .arg (s.duckBelowHz.load() > 0.0f ? QStringLiteral ("↓") : QString()));
        for (int r = 0; r < mixer::numReturns; ++r)
            sendLabels[(size_t) r]->setText (dbText (s.sendDb[(size_t) r].load()) + (s.sendPre[(size_t) r].load() ? MixerWindow::tr (" pre") : QString()));
        faderValue->setText (dbText (s.faderDb.load()));
    }

    void tick (double seconds)
    {
        meter->setLevels (window.engine.takeMeter (index), seconds);
        peakValue->setText (peakText (meter->highestPeak()));
    }

    // Picked strips (Ctrl+click, for Q-Link) have a dashed border, the selected one a solid one.
    void setSelected (bool on, bool picked = false)
    {
        const auto colour = Theme::current().color (QStringLiteral ("highlight")).name();
        setStyleSheet (on ? QStringLiteral ("QFrame#strip { border: 2px solid %1; }").arg (colour)
                          : picked ? QStringLiteral ("QFrame#strip { border: 2px dashed %1; }").arg (colour) : QString());
        auto f = number->font();
        f.setBold (on);
        number->setFont (f);
    }

    void channelMenu (QPoint at = {})
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
        menu.exec (at.isNull() ? channelsButton->mapToGlobal (QPoint (0, channelsButton->height())) : at);
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
            else if (o == widthDial)
                widthDial->setValue (100);
            else if (o == sendDials[0] || o == sendDials[1] || o == duckDial)
                static_cast<QDial*> (o)->setValue (0);
            else if (o == audioLayerDial || o == instLayerDial)
                static_cast<QDial*> (o)->setValue (faderPosition (0.0));
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
        if (e->modifiers() & Qt::ControlModifier)
            window.pick (index, true);
        else
        {
            window.select (index);
            window.engageTrack();
            window.showDevices();   // a click on the strip brings its Device Chain back from a clip
        }
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
        if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat (MixerWindow::pluginMime()))
            e->acceptProposedAction();
    }

    void dropEvent (QDropEvent* e) override
    {
        // An instrument or effect from the browser goes to its place (owner, 2026-10-25).
        if (e->mimeData()->hasFormat (MixerWindow::pluginMime()))
        {
            if (window.dropPlugin (index, e->mimeData()->data (MixerWindow::pluginMime())))
                e->acceptProposedAction();
            return;
        }
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
    bool compact = false;
    const bool inspecting;
    QWidget* panColumnPart = nullptr;
    static constexpr int compactFader = 110, compactSlot = 18;
    QWidget* instrumentPart;
    QWidget* prePart;
    QWidget* insertPart;
    QWidget* layerPart;
    std::array<QPushButton*, 3> modeButtons {};
    QDial* audioLayerDial;
    QDial* instLayerDial;
    QDial* duckDial;
    QLabel* audioLayerLabel;
    QLabel* instLayerLabel;
    QLabel* duckLabel;
    QWidget* audioLayerPart;
    QWidget* instLayerPart;
    QWidget* sendPart;
    std::array<QDial*, mixer::numReturns> sendDials {};
    std::array<QLabel*, mixer::numReturns> sendLabels {};
    QWidget* panPart;
    QWidget* widthPart;
    QWidget* trimPart;
    QPushButton* lowCutButton;
    QPushButton* highCutButton;
    QDoubleSpinBox* delayBox;
    QPushButton* addInsertButton;
    QListWidget* insertList;
    QDial* widthDial;
    QLabel* widthLabel;
    QFrame* colourBar;
    QFrame* nameBox;
    QWidget* modePart;
    QLabel* insertTitle;
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
// PHI rāga's window: its own raga (when the track's tuning is not a raga), Sa, and the sound.
QDialog* ragaDialog (mixer::InstrumentPtr inst, const QString& title, QWidget* parent, std::function<void()> changed)
{
    auto& raga = static_cast<mixer::RagaInstrument&> (*inst);
    auto* d = new QDialog (parent);
    d->setAttribute (Qt::WA_DeleteOnClose);
    d->setWindowTitle (QString::fromUtf8 ("PHI rāga: %1").arg (title));
    auto* form = new QFormLayout (d);
    const auto s = raga.settings();
    auto* ragas = new QComboBox (d);
    for (const auto& r : phi::builtInRagas())
        ragas->addItem (QString::fromStdString (r.name), QString::fromStdString (r.plain));
    ragas->setCurrentIndex (std::max (0, ragas->findData (QString::fromStdString (s.raga))));
    ragas->setMaxVisibleItems (24);
    ragas->setToolTip (QObject::tr ("The raga it plays when the track's tuning (Inspector) is not a raga"));
    auto* sa = new QComboBox (d);
    for (const auto* n : { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" })
        sa->addItem (QString::fromUtf8 (n));
    sa->setCurrentIndex (s.sa);
    auto percent = [d] (double v, const QString& tip)
    {
        auto* b = new QSpinBox (d);
        b->setRange (0, 100);
        b->setSuffix (QObject::tr (" %"));
        b->setValue ((int) std::lround (v * 100.0));
        b->setToolTip (tip);
        return b;
    };
    auto* ornaments = percent (s.ornaments, QObject::tr ("How much of the sheet's ornaments (āndolan, kaṇ, khaṭkā, murkī, gamak...) it plays"));
    auto* jawari = percent (s.jawari, QObject::tr ("The bridge: open (khulā) to closed (band), more buzz"));
    auto* tarab = percent (s.tarab, QObject::tr ("The sympathetic strings, tuned to the raga"));
    auto* meend = new QCheckBox (QString::fromUtf8 ("Legato plays mīṇḍ: a key pressed while another is held pulls the string"), d);
    meend->setChecked (s.meend);
    auto* meendTime = new QDoubleSpinBox (d);
    meendTime->setRange (0.02, 3.0);
    meendTime->setSingleStep (0.05);
    meendTime->setSuffix (QObject::tr (" s"));
    meendTime->setValue (s.meendSeconds);
    auto* tanpura = new QCheckBox (QString::fromUtf8 ("Tānpūrā drone: Pa (or ma, or ni), Sa, Sa and kharaj Sa, over and over"), d);
    tanpura->setChecked (s.tanpura);
    auto* tanpuraLevel = percent (s.tanpuraLevel, QString::fromUtf8 ("The tānpūrā's level"));
    auto* tanpuraTime = new QDoubleSpinBox (d);
    tanpuraTime->setRange (1.0, 12.0);
    tanpuraTime->setSingleStep (0.5);
    tanpuraTime->setSuffix (QObject::tr (" s"));
    tanpuraTime->setValue (s.tanpuraSeconds);
    tanpuraTime->setToolTip (QObject::tr ("One round of its four strings"));
    auto* now = new QLabel (d);
    now->setWordWrap (true);
    form->addRow (QObject::tr ("&Raga"), ragas);
    form->addRow (QObject::tr ("&Sa"), sa);
    form->addRow (QObject::tr ("&Ornaments"), ornaments);
    form->addRow (QString::fromUtf8 ("&Jawārī"), jawari);
    form->addRow (QObject::tr ("&Tarab"), tarab);
    form->addRow (meend);
    form->addRow (QString::fromUtf8 ("&Mīṇḍ time"), meendTime);
    form->addRow (tanpura);
    form->addRow (QString::fromUtf8 ("Tānpūrā &level"), tanpuraLevel);
    form->addRow (QString::fromUtf8 ("Tānpūrā r&ound"), tanpuraTime);
    form->addRow (now);
    auto* help = new QLabel (QObject::tr ("Keys play the raga's svaras by the way the melody moves: its āroha going up, its avaroha coming down. "
                                          "Keys two octaves and more below Sa strike the chikārī. MPE controllers bend each note on its own."), d);
    help->setWordWrap (true);
    form->addRow (help);
    auto show = [now, &raga] { now->setText (QObject::tr ("Playing %1.").arg (QString::fromStdString (raga.playing()))); };
    auto apply = [=, &raga]
    {
        mixer::RagaSettings n;
        n.raga = ragas->currentData().toString().toStdString();
        n.sa = sa->currentIndex();
        n.ornaments = ornaments->value() / 100.0;
        n.jawari = jawari->value() / 100.0;
        n.tarab = tarab->value() / 100.0;
        n.meend = meend->isChecked();
        n.meendSeconds = meendTime->value();
        n.tanpura = tanpura->isChecked();
        n.tanpuraLevel = tanpuraLevel->value() / 100.0;
        n.tanpuraSeconds = tanpuraTime->value();
        raga.setSettings (n);
        show();
        changed();
    };
    show();
    auto keep = std::make_shared<mixer::InstrumentPtr> (inst);
    QObject::connect (d, &QObject::destroyed, d, [keep] { keep->reset(); });
    for (auto* c : { ragas, sa })
        QObject::connect (c, &QComboBox::currentIndexChanged, d, apply);
    for (auto* b : { ornaments, jawari, tarab, tanpuraLevel })
        QObject::connect (b, &QSpinBox::valueChanged, d, apply);
    for (auto* c : { meend, tanpura })
        QObject::connect (c, &QCheckBox::toggled, d, apply);
    for (auto* b : { meendTime, tanpuraTime })
        QObject::connect (b, &QDoubleSpinBox::valueChanged, d, apply);
    return d;
}

//==============================================================================
QDialog* synthDialog (mixer::InstrumentPtr inst, const QString& title, QWidget* parent, std::function<void()> changed)
{
    auto& synth = static_cast<mixer::Synth&> (*inst);
    auto* d = new QDialog (parent);
    d->setAttribute (Qt::WA_DeleteOnClose);
    d->setWindowTitle (QObject::tr ("Synth: %1").arg (title));
    auto* form = new QFormLayout (d);
    const auto s = synth.settings();
    auto* wave = new QComboBox (d);
    wave->addItems ({ QObject::tr ("Sine"), QObject::tr ("Triangle"), QObject::tr ("Saw"), QObject::tr ("Square") });
    wave->setCurrentIndex (s.wave);
    auto seconds = [d] (double v, double max)
    {
        auto* b = new QDoubleSpinBox (d);
        b->setRange (0.0, max);
        b->setDecimals (3);
        b->setSingleStep (0.01);
        b->setSuffix (QObject::tr (" s"));
        b->setValue (v);
        return b;
    };
    auto* attack = seconds (s.attack, 10.0);
    auto* decay = seconds (s.decay, 10.0);
    auto* sustain = new QSpinBox (d);
    sustain->setRange (0, 100);
    sustain->setSuffix (QObject::tr (" %"));
    sustain->setValue ((int) std::lround (s.sustain * 100.0));
    auto* release = seconds (s.release, 20.0);
    auto* cutoff = new QSpinBox (d);
    cutoff->setRange (40, 20000);
    cutoff->setSingleStep (100);
    cutoff->setSuffix (QObject::tr (" Hz"));
    cutoff->setValue ((int) s.cutoff);
    auto* gain = new QDoubleSpinBox (d);
    gain->setRange (-48.0, 12.0);
    gain->setSuffix (QObject::tr (" dB"));
    gain->setValue (s.gainDb);
    auto* voices = new QSpinBox (d);
    voices->setRange (1, mixer::Synth::maxVoices);
    voices->setValue (s.voices);
    auto* drums = new QCheckBox (QObject::tr ("Channel 10 plays General MIDI drums"), d);
    drums->setChecked (s.drums);
    form->addRow (QObject::tr ("&Wave"), wave);
    form->addRow (QObject::tr ("&Attack"), attack);
    form->addRow (QObject::tr ("&Decay"), decay);
    form->addRow (QObject::tr ("&Sustain"), sustain);
    form->addRow (QObject::tr ("&Release"), release);
    form->addRow (QObject::tr ("&Low-pass"), cutoff);
    form->addRow (QObject::tr ("&Gain"), gain);
    form->addRow (QObject::tr ("&Voices"), voices);
    form->addRow (drums);
    auto apply = [=, &synth]
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
    QObject::connect (d, &QObject::destroyed, d, [keep] { keep->reset(); });
    QObject::connect (wave, &QComboBox::currentIndexChanged, d, apply);
    for (auto* b : { attack, decay, release, gain })
        QObject::connect (b, &QDoubleSpinBox::valueChanged, d, apply);
    for (auto* b : { sustain, cutoff, voices })
        QObject::connect (b, &QSpinBox::valueChanged, d, apply);
    QObject::connect (drums, &QCheckBox::toggled, d, apply);
    return d;
}

//==============================================================================
// The tracks across time, one horizontal row each, holding the track's clips: audio
// clips show their waveform, MIDI clips their notes. Clips are dragged with the mouse
// in time and between tracks (Ctrl copies; Shift leaves the grid), trimmed at their
// edges, and dropped on from the browser or the file manager. Keys (when the rows have
// the focus): Left/Right move the playhead a beat (Ctrl a bar), Up/Down pick the track,
// Delete removes the selected clips, Ctrl+E splits at the playhead, Ctrl+D duplicates,
// Shift+M mutes them, G turns the grid on and off. Ctrl+wheel zooms, the wheel scrolls.
// `one`: the editor's single track (MixerWindow::editorTrack), its row the whole height.
class TrackLanes final : public QWidget
{
public:
    explicit TrackLanes (MixerWindow& w, bool one = false, QWidget* parent = nullptr)
        : QWidget (parent != nullptr ? parent : &w), window (w), single (one)
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
        const auto barFrames = std::max (1, window.engine.beatsPerBar.load()) * beatFrames();
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
        for (int i = firstRow(); i < firstRow() + rowCount(); ++i)
        {
            const QRect row (0, rowTop (i), width(), rowH);
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
            // A take onto the timeline: red from where it began to the playhead.
            if (const auto take = window.engine.recordInfo (i); take.scene < 0 && take.state == mixer::RecordState::recording)
            {
                const auto from = xOf ((double) take.start), to = xOf ((double) window.engine.position());
                QColor red (0xe0, 0x40, 0x40);
                red.setAlpha (90);
                g.fillRect (QRectF (std::min (from, to), lane.top(), std::abs (to - from), lane.height()), red);
                g.setPen (QColor (0xe0, 0x40, 0x40));
                g.setFont (smallFont());
                g.drawText (QRectF (std::min (from, to) + 4, lane.top(), std::max (60.0, std::abs (to - from)), lane.height()),
                            Qt::AlignVCenter | Qt::AlignLeft, MixerWindow::tr ("● Recording"));
            }
            else if (take.scene < 0 && take.state == mixer::RecordState::waiting)
            {
                g.setPen (QColor (0xe0, 0x40, 0x40));
                g.setFont (smallFont());
                g.drawText (lane.adjusted (8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                            window.engine.countingIn() ? MixerWindow::tr ("● Counting in...") : MixerWindow::tr ("● Waiting for the transport"));
            }
            if (list.empty() && rowH >= 30 && window.engine.recordInfo (i).state == mixer::RecordState::none)
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
            g.drawLine (QPointF (px, 0), QPointF (px, rulerHeight + rowCount() * rowH));
        }
        if (dropHint.x() >= headerWidth)
        {
            const int row = rowAt (dropHint.y());
            const auto x = xOf ((double) snapped (frameAt (dropHint.x()), false));
            if (row >= 0)
            {
                g.setPen (QPen (Theme::current().color (QStringLiteral ("highlight")), 2.0));
                g.drawLine (QPointF (x, rowTop (row)), QPointF (x, rowTop (row) + rowH));
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
        if (single)
            Q_EMIT window.activated();
        const auto pos = e->position();
        const int row = rowAt (pos.y());
        if (row >= 0 && pos.y() >= rulerHeight)
        {
            window.select (row);
            window.engageTrack();
        }
        drag = Drag::none;
        pressPos = pos;
        moved = false;
        if (e->button() != Qt::LeftButton || pos.x() < headerWidth)
        {
            if (e->button() == Qt::LeftButton)
                window.showDevices();   // a track's name: its devices
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
        window.showTimelineClip (hit.track, hit.id);   // drawn in the Device Chain's place
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
                    const auto step = ctrl ? std::max (1, window.engine.beatsPerBar.load()) * beatFrames() : beatFrames();
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
            menu.addSeparator();
            const auto list = window.engine.clips (row);
            menu.addAction (MixerWindow::tr ("Drums to &MIDI"), this, [this, row, id = hit.id] { window.drumsToMidi (row, id, -1); })
                ->setEnabled (std::any_of (list.begin(), list.end(), [id = hit.id] (const mixer::Clip& c) { return c.id == id && c.audio != nullptr; }));
            menu.addAction (MixerWindow::tr ("Send to Clip &Launcher"), this, [this, row, id = hit.id] { window.clipToLauncher (row, id); });
            Frame from = 0;
            for (const auto& c : list)
                if (c.id == hit.id)
                    from = c.start;
            menu.addAction (MixerWindow::tr ("minimalize from here..."), this, [this, from] { window.minimalizeTimeline (from); });
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
            menu.addAction (MixerWindow::tr ("minimalize from here..."), this, [this, at] { window.minimalizeTimeline (at); });
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
        const auto at = snapped (frameAt (std::max ((double) headerWidth, e->position().x())), free);
        if (single && files.size() == 1 && window.audioIntoMidiTrack (row, files.front(), -1, at))
        {
            e->acceptProposedAction();
            update();
            return;
        }
        window.addFiles (row, files, at);
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

    // The rows shown: every track, or the editor's one.
    int firstRow() const { return single ? window.editorTrack() : 0; }
    int rowCount() const { return single ? 1 : window.engine.numTracks(); }
    int rowTop (int track) const { return rulerHeight + (track - firstRow()) * rowHeight(); }

    int rowHeight() const
    {
        const auto count = std::max (1, rowCount());
        return std::clamp ((height() - rulerHeight) / count, 22, single ? 600 : 140);
    }

    int rowAt (double y) const
    {
        if (single)
            return firstRow();
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
        const auto top = rowTop (track) + 2.0 + layer * height;
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
    bool single = false;           // the editor's one track
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
// The browser's Instruments and Audio Effects: dragged onto a strip or the Device Chain.
class PluginTree final : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return { MixerWindow::pluginMime() }; }

    QMimeData* mimeData (const QList<QTreeWidgetItem*>& items) const override
    {
        if (items.isEmpty() || items.front()->data (0, Qt::UserRole).toByteArray().isEmpty())
            return nullptr;
        auto* m = new QMimeData;
        m->setData (MixerWindow::pluginMime(), items.front()->data (0, Qt::UserRole).toByteArray());
        return m;
    }
};

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

        // Or the instruments and effects (owner, 2026-10-25), to drag onto a strip or the
        // Device Chain: an instrument takes the instrument's place, an effect joins the inserts.
        plugins = new PluginTree (this);
        plugins->setHeaderHidden (true);
        plugins->setDragEnabled (true);
        plugins->setDragDropMode (QAbstractItemView::DragOnly);
        plugins->setSelectionMode (QAbstractItemView::SingleSelection);
        plugins->setAccessibleName (MixerWindow::tr ("Instruments and effects"));
        plugins->installEventFilter (this);
        plugins->hide();
        layout->addWidget (plugins, 1);

        hint = new QLabel (this);
        hint->setWordWrap (true);
        hint->setEnabled (false);
        layout->addWidget (hint);

        favourites = appSettings().value (QStringLiteral ("mixer/favourites")).toStringList();
        fillPlaces();
        setRoot (appSettings().value (QStringLiteral ("mixer/browserRoot"), window.folder()).toString());

        QObject::connect (places, &QComboBox::activated, this, [this] (int i)
        {
            const auto place = places->itemData (i).toString();
            if (place.startsWith (QLatin1String ("plugins:")))
            {
                showPlugins (place.mid (8));
                plugins->setFocus();
                return;
            }
            setRoot (place);
            tree->setFocus();
        });
        QObject::connect (upButton, &QPushButton::clicked, this, [this] { goUp(); });
        QObject::connect (plugins, &QTreeWidget::itemActivated, this, [this] (QTreeWidgetItem* item) { usePlugin (item); });
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
        if (! pluginKind.isEmpty())
            hint->setText (MixerWindow::tr ("Drag onto a strip or the Device Chain, or Enter puts it on track %1").arg (track));
        else
            hint->setText (MixerWindow::tr ("Enter puts the file on track %1 at the playhead").arg (track));
    }

protected:
    bool eventFilter (QObject* o, QEvent* e) override
    {
        if (o == plugins && e->type() == QEvent::KeyPress)
        {
            auto* k = static_cast<QKeyEvent*> (e);
            if (k->key() == Qt::Key_Backspace)
            {
                goUp();
                return true;
            }
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
            {
                usePlugin (plugins->currentItem());
                return true;
            }
        }
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
        places->insertSeparator (places->count());
        places->addItem (MixerWindow::tr ("Instruments"), QStringLiteral ("plugins:instruments"));
        places->addItem (MixerWindow::tr ("Audio Effects"), QStringLiteral ("plugins:effects"));
        if (! pluginKind.isEmpty())
            places->setCurrentIndex (places->findData (QStringLiteral ("plugins:") + pluginKind));
        else if (! root.isEmpty())
        {
            if (places->findData (root) < 0)
                places->addItem (root, root);
            places->setCurrentIndex (places->findData (root));
        }
    }

    // The instruments (the built-in ones, then the plugins found) or the effects, by format.
    void showPlugins (const QString& kind)
    {
        pluginKind = kind;
        plugins->clear();
        std::map<QString, QTreeWidgetItem*> groups;
        auto group = [&] (const QString& name)
        {
            auto& g = groups[name];
            if (g == nullptr)
            {
                g = new QTreeWidgetItem (plugins, { name });
                g->setFlags (Qt::ItemIsEnabled);
            }
            return g;
        };
        auto add = [&] (const QString& groupName, const QString& name, const QByteArray& what)
        {
            auto* item = new QTreeWidgetItem (group (groupName), { name });
            item->setData (0, Qt::UserRole, what);
            item->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        };
        if (kind == QLatin1String ("instruments"))
        {
            add (MixerWindow::tr ("Built-in"), MixerWindow::tr ("Synth"), "synth");
            add (MixerWindow::tr ("Built-in"), MixerWindow::tr ("Sampler"), "sampler");
            add (MixerWindow::tr ("Built-in"), MixerWindow::tr ("Slicer"), "slicer");
            add (MixerWindow::tr ("Built-in"), QString::fromUtf8 ("PHI rāga"), "raga");
            for (const auto& p : window.audio.knownInstruments())
                add (QString::fromStdString (p.format), QString::fromStdString (p.name), "instrument\n" + QByteArray::fromStdString (p.id));
        }
        else
        {
            auto list = window.audio.knownPlugins();
            std::sort (list.begin(), list.end(), [] (const PluginInfo& x, const PluginInfo& y)
            {
                return QString::fromStdString (x.name).compare (QString::fromStdString (y.name), Qt::CaseInsensitive) < 0;
            });
            group (QStringLiteral ("Built-in"));   // first
            for (const auto& p : list)
                add (QString::fromStdString (p.format), QString::fromStdString (p.name), "effect\n" + QByteArray::fromStdString (p.id));
        }
        plugins->expandAll();
        if (plugins->topLevelItemCount() > 0 && plugins->topLevelItem (0)->childCount() > 0)
            plugins->setCurrentItem (plugins->topLevelItem (0)->child (0));
        tree->hide();
        plugins->show();
        favouriteButton->setEnabled (false);
        fillPlaces();
        showHint();
    }

    void usePlugin (QTreeWidgetItem* item)
    {
        if (item == nullptr || item->data (0, Qt::UserRole).toByteArray().isEmpty())
            return;
        const auto track = window.selected < mixer::masterIndex ? window.selected : 0;
        window.dropPlugin (track, item->data (0, Qt::UserRole).toByteArray());
    }

    void setRoot (const QString& folder)
    {
        pluginKind.clear();
        plugins->hide();
        tree->show();
        favouriteButton->setEnabled (true);
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
        if (! pluginKind.isEmpty())
        {
            setRoot (root);   // back to the files
            tree->setFocus();
            return;
        }
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
        if (window.launcherShown())
            window.sendToLauncher (file, {}, {});   // the selected track's next empty slot
        else
            window.addFiles (track, { file }, window.engine.position(), movePlayhead);
    }

    MixerWindow& window;
    QComboBox* places;
    QPushButton* upButton;
    QPushButton* favouriteButton;
    QFileSystemModel* model;
    QTreeView* tree;
    PluginTree* plugins;
    QString pluginKind;   // "instruments" or "effects" while those show instead of the files
    QLabel* hint;
    QString root;
    QStringList favourites;
};

//==============================================================================
// The clip launcher (docs/clip-launcher.md): a grid of slots, tracks across and scenes
// down, with each scene's launch button on the right and a stop button under each track.
namespace
{
    // Launch quantise choices, in beats of 4/4.
    constexpr double quantiseBeats[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
    constexpr int numQuantise = (int) std::size (quantiseBeats);

    QString quantiseName (int index)
    {
        switch (index)
        {
            case 0:  return MixerWindow::tr ("Off");
            case 1:  return MixerWindow::tr ("1/16");
            case 2:  return MixerWindow::tr ("1/8");
            case 3:  return MixerWindow::tr ("1/4");
            case 4:  return MixerWindow::tr ("1/2");
            case 5:  return MixerWindow::tr ("1 Bar");
            default: return MixerWindow::tr ("%1 Bars").arg ((int) (quantiseBeats[std::clamp (index, 0, numQuantise - 1)] / 4.0));
        }
    }

    int quantiseIndex (double beats)
    {
        int best = 0;
        for (int i = 1; i < numQuantise; ++i)
            if (std::abs (quantiseBeats[i] - beats) < std::abs (quantiseBeats[best] - beats))
                best = i;
        return best;
    }

    QColor inkOn (const QColor& c) { return c.lightnessF() > 0.55 ? QColor (17, 17, 17) : QColor (244, 244, 244); }
}

// `one`: the editor's single track (MixerWindow::editorTrack), its slots left to right, the
// track's name and stop button on their left, and no scenes.
class ClipGrid final : public QWidget
{
public:
    explicit ClipGrid (MixerWindow& w, bool one = false, QWidget* parent = nullptr)
        : QWidget (parent != nullptr ? parent : &w), window (w), single (one)
    {
        setAcceptDrops (true);
        setFocusPolicy (Qt::NoFocus);
        setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
        updateMinimum();
    }

    QSize sizeHint() const override
    {
        if (single)
            return { sceneWidth + window.numScenes * singleColumn + 2 * margin, 104 };
        return { 8 * maxColumn + sceneWidth + 2 * margin, headerHeight + stopHeight + window.numScenes * 30 + 2 * margin };
    }
    void updateMinimum()
    {
        if (single)
        {
            setMinimumSize (sceneWidth + window.numScenes * singleColumn + 2 * margin, minRow + 2 * margin);
            return;
        }
        setMinimumHeight (headerHeight + stopHeight + window.numScenes * minRow + 2 * margin);
    }
    QRect sceneArea (int scene) const { return sceneRect (std::clamp (scene, 0, std::max (0, window.numScenes - 1))); }
    QRect slotArea (int scene) const { return slotRect (firstTrack(), std::clamp (scene, 0, std::max (0, window.numScenes - 1))); }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter p (this);
        p.setRenderHint (QPainter::Antialiasing);
        const auto& theme = Theme::current();
        const auto base = theme.color (QStringLiteral ("base")), mid = theme.color (QStringLiteral ("mid"));
        const auto text = theme.color (QStringLiteral ("text")), highlight = theme.color (QStringLiteral ("highlight"));
        // In a see-through window only the track names and the clips stay solid (owner, 2026-10-25);
        // the background and the scene and stop buttons fade with the window.
        const auto button = Theme::withOpacity (theme.color (QStringLiteral ("button"))), go = theme.color (QStringLiteral ("meterLow"));
        const auto disabled = theme.color (QStringLiteral ("disabledText"));
        p.fillRect (rect(), Theme::withOpacity (base));
        const bool blink = (QDateTime::currentMSecsSinceEpoch() / 250) % 2 == 0;
        const auto tracks = window.engine.numTracks();
        auto bold = font();
        bold.setBold (true);
        auto small = font();
        small.setPointSizeF (std::max (6.0, small.pointSizeF() * 0.85));

        auto triangle = [&p] (const QRectF& r, const QColor& c)
        {
            const auto s = std::min (r.height(), r.width()) * 0.42;
            const auto centre = r.center();
            const QPointF points[] { { centre.x() - s * 0.5, centre.y() - s * 0.6 }, { centre.x() - s * 0.5, centre.y() + s * 0.6 }, { centre.x() + s * 0.6, centre.y() } };
            p.setPen (Qt::NoPen);
            p.setBrush (c);
            p.drawPolygon (points, 3);
        };
        auto square = [&p] (const QRectF& r, const QColor& c)
        {
            const auto s = std::min (r.height(), r.width()) * 0.34;
            p.setPen (Qt::NoPen);
            p.setBrush (c);
            p.drawRect (QRectF (r.center().x() - s * 0.5, r.center().y() - s * 0.5, s, s));
        };

        const QColor red (0xe0, 0x40, 0x40);
        auto dot = [&p] (const QRectF& r, const QColor& c)
        {
            const auto s = std::min (r.height(), r.width()) * 0.22;
            p.setPen (Qt::NoPen);
            p.setBrush (c);
            p.drawEllipse (r.center(), s, s);
        };
        const auto armedTrack = window.engine.liveTrack();
        for (int t = firstTrack(); t < firstTrack() + trackCount(); ++t)
        {
            const auto& info = window.info[(size_t) t];
            const auto launch = window.engine.launchInfo (t);
            const auto take = window.engine.recordInfo (t);
            const auto head = headerRect (t);
            p.setPen (Qt::NoPen);
            p.setBrush (info.colour);
            p.drawRoundedRect (head, 3, 3);
            p.setFont (bold);
            p.setPen (inkOn (info.colour));
            const auto label = QStringLiteral ("%1 %2").arg (t + 1).arg (info.name);
            p.drawText (head.adjusted (6, 0, -24, 0), Qt::AlignVCenter | Qt::AlignLeft,
                        p.fontMetrics().elidedText (label, Qt::ElideRight, head.width() - 30));
            if (! launch.timeline)
                p.drawText (backRect (t), Qt::AlignCenter, QStringLiteral ("⟲"));   // click: back to the timeline
            if (t == window.selected)
            {
                p.setBrush (highlight);
                p.drawRect (QRect (head.left(), head.bottom() + 1, head.width(), 2));
            }

            for (int s = 0; s < window.numScenes; ++s)
            {
                const auto r = slotRect (t, s);
                const auto& slot = window.slots[(size_t) t][(size_t) s];
                const bool playing = launch.playing == s, queued = launch.queued == s;
                const bool active = take.scene == s && (take.state == mixer::RecordState::waiting || take.state == mixer::RecordState::recording);
                const bool layering = active && window.layerTakes[(size_t) t].scene == s && slot != nullptr;
                const bool taking = active && ! layering;
                if (taking)
                {
                    // A take: red, blinking while it waits for its first beat, filling as it records.
                    const bool waiting = take.state == mixer::RecordState::waiting;
                    p.setPen (QPen (red, 2));
                    p.setBrush (waiting && blink ? Qt::NoBrush : QBrush (red.darker (160)));
                    p.drawRoundedRect (QRectF (r).adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
                    dot (QRectF (r.left(), r.top(), playWidth, r.height()), red.lighter (130));
                    p.setFont (small);
                    p.setPen (Qt::white);
                    const auto bar = 60.0 / std::max (1.0, window.engine.tempo.load()) * window.rate() * std::max (1, window.engine.beatsPerBar.load());
                    const auto label = waiting ? MixerWindow::tr ("Waiting...")
                                     : take.length > 0 ? MixerWindow::tr ("Rec bar %1 of %2").arg ((int) ((double) take.recorded / bar) + 1).arg ((int) std::lround ((double) take.length / bar))
                                                       : MixerWindow::tr ("Rec bar %1").arg ((int) ((double) take.recorded / bar) + 1);
                    p.drawText (r.adjusted (playWidth, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText (label, Qt::ElideRight, r.width() - playWidth - 4));
                    if (! waiting && take.length > 0)
                    {
                        p.setPen (Qt::NoPen);
                        p.setBrush (red.lighter (130));
                        p.drawRect (QRectF (r.left() + 2, r.bottom() - 3, (r.width() - 4) * (double) take.progress, 3));
                    }
                }
                else if (slot != nullptr)
                {
                    auto fill = info.colour;
                    if (queued && blink)
                        fill = fill.lighter (150);
                    p.setPen (playing ? QPen (go, 2) : Qt::NoPen);
                    p.setBrush (fill);
                    p.drawRoundedRect (QRectF (r).adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
                    const auto ink = inkOn (fill);
                    const QRectF play (r.left(), r.top(), playWidth, r.height());
                    if (playing)
                        triangle (play, go.darker (110));
                    else
                        triangle (play, ink);
                    QString right;
                    if (playing && launch.passes > 0)
                        right = QStringLiteral ("×%1").arg (launch.passes + 1);
                    auto name = window.slotName (t, s);
                    if (! slot->loop)
                        name = QStringLiteral ("→ ") + name;   // plays once
                    if (splitSlots())
                    {
                        // Two halves (owner, 2026-10-26): the MIDI layer's notes on top, the audio's
                        // waveform below, the name over them; an empty half on the armed track has
                        // its record dot.
                        // A clip of one kind fills the slot (owner, 2026-10-26: audio only, MIDI
                        // only, or both split in half).
                        auto faint = ink;
                        faint.setAlpha (110);
                        const auto top = halfRect (t, s, false), bottom = halfRect (t, s, true);
                        const bool mixed = slot->clip.audio != nullptr && slot->clip.midi != nullptr;
                        const QRectF whole (top.left(), top.top(), top.width(), bottom.bottom() - top.top());
                        drawNotes (p, mixed ? top : whole, slot->clip, faint);
                        drawWave (p, mixed ? bottom : whole, slot->clip, faint);
                        if (mixed)
                        {
                            p.setPen (QPen (faint, 1, Qt::DotLine));
                            p.drawLine (QPointF (top.left(), top.bottom()), QPointF (top.right(), top.bottom()));
                        }
                        for (const bool audio : { false, true })
                        {
                            const auto half = audio ? bottom : top;
                            const bool empty = audio ? slot->clip.audio == nullptr : slot->clip.midi == nullptr;
                            if (layering && window.layerTakes[(size_t) t].audio == audio)
                            {
                                const bool waiting = take.state == mixer::RecordState::waiting;
                                auto glow = red;
                                glow.setAlpha (waiting && blink ? 60 : 150);
                                p.setPen (Qt::NoPen);
                                p.setBrush (glow);
                                p.drawRect (half);
                                p.setBrush (red.lighter (130));
                                if (! waiting)
                                    p.drawRect (QRectF (half.left(), half.bottom() - 2, half.width() * (double) take.progress, 2));
                                dot (layerDotRect (t, s, audio), Qt::white);
                            }
                            else if (empty && t == armedTrack && take.state == mixer::RecordState::none)
                                dot (layerDotRect (t, s, audio), red.darker (120));   // records this layer
                        }
                        p.setFont (small);
                        p.setPen (ink);
                        const auto rightWidth = right.isEmpty() ? 0 : p.fontMetrics().horizontalAdvance (right) + 4;
                        p.drawText (top.adjusted (2, 0, -rightWidth - top.height(), 0), Qt::AlignVCenter | Qt::AlignLeft,
                                    p.fontMetrics().elidedText (name, Qt::ElideRight, (int) top.width() - rightWidth - (int) top.height() - 2));
                        if (! right.isEmpty())
                            p.drawText (top.adjusted (0, 0, -top.height(), 0), Qt::AlignVCenter | Qt::AlignRight, right);
                    }
                    else
                    {
                        p.setFont (font());
                        p.setPen (ink);
                        const auto rightWidth = right.isEmpty() ? 0 : p.fontMetrics().horizontalAdvance (right) + 6;
                        p.drawText (r.adjusted (playWidth, 0, -4 - rightWidth, 0), Qt::AlignVCenter | Qt::AlignLeft,
                                    p.fontMetrics().elidedText (name, Qt::ElideRight, r.width() - playWidth - 6 - rightWidth));
                        if (! right.isEmpty())
                            p.drawText (r.adjusted (0, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight, right);
                    }
                    if (playing)
                    {
                        p.setPen (Qt::NoPen);
                        p.setBrush (go);
                        p.drawRect (QRectF (r.left() + 2, r.bottom() - 3, (r.width() - 4) * (double) launch.progress, 3));
                    }
                    if (playing && take.state == mixer::RecordState::overdubbing && take.scene == s)
                    {
                        // Overdubbing: a red edge, and its dot where the play button is.
                        p.setPen (QPen (red, 2));
                        p.setBrush (Qt::NoBrush);
                        p.drawRoundedRect (QRectF (r).adjusted (1, 1, -1, -1), 3, 3);
                        dot (QRectF (r.left(), r.top(), playWidth, r.height()), blink ? red : red.darker (130));
                    }
                }
                else
                {
                    p.setPen (QPen (mid, 1, Qt::DashLine));
                    p.setBrush (Qt::NoBrush);
                    p.drawRoundedRect (QRectF (r).adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
                    if (window.loadingSlots.count ({ t, s }) != 0)
                    {
                        p.setPen (disabled);
                        p.setFont (small);
                        p.drawText (r, Qt::AlignCenter, MixerWindow::tr ("Loading..."));
                    }
                    else if (t == armedTrack)
                        dot (QRectF (r.left(), r.top(), playWidth, r.height()), red.darker (120));   // records into the slot
                    else if (launch.playing >= 0)
                        square (QRectF (r.left(), r.top(), playWidth, r.height()), mid);   // a scene launched here stops the track
                }
                if (t == window.selected && s == window.cursorScene)
                {
                    p.setPen (QPen (highlight, 2));
                    p.setBrush (Qt::NoBrush);
                    p.drawRoundedRect (QRectF (r).adjusted (-1.5, -1.5, 1.5, 1.5), 4, 4);
                }
            }

            const auto stop = stopRect (t);
            const bool stopping = launch.queued == mixer::stopQueued;
            p.setPen (QPen (mid, 1));
            p.setBrush (stopping && blink ? mid : button);
            p.drawRoundedRect (QRectF (stop).adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
            square (stop, launch.playing >= 0 ? text : disabled);
        }
        if (single)
            return;   // the scenes are the mixer's

        // The scenes, and Stop All.
        p.setFont (bold);
        p.setPen (text);
        p.drawText (sceneHeaderRect(), Qt::AlignCenter, MixerWindow::tr ("Scenes"));
        for (int s = 0; s < window.numScenes; ++s)
        {
            const auto r = sceneRect (s);
            p.setPen (s == window.cursorScene ? QPen (highlight, 2) : QPen (mid, 1));
            p.setBrush (button);
            p.drawRoundedRect (QRectF (r).adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
            triangle (QRectF (r.left(), r.top(), playWidth, r.height()), go);
            p.setFont (font());
            p.setPen (text);
            p.drawText (r.adjusted (playWidth, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                        p.fontMetrics().elidedText (window.sceneName (s), Qt::ElideRight, r.width() - playWidth - 4));
        }
        const auto all = stopAllRect();
        p.setPen (QPen (mid, 1));
        p.setBrush (button);
        p.drawRoundedRect (QRectF (all).adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
        square (QRectF (all.left(), all.top(), playWidth, all.height()), text);
        p.setPen (text);
        p.drawText (all.adjusted (playWidth, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, MixerWindow::tr ("Stop All"));
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        if (single)
            Q_EMIT window.activated();   // the keyboard to the mixer's armed track
        const auto hit = hitAt (e->position().toPoint());
        if (e->button() == Qt::RightButton)
            return;   // the context menu
        switch (hit.kind)
        {
            case Hit::header: window.select (hit.track); window.engageTrack(); window.moveCursor (hit.track, window.cursorScene); window.showDevices(); break;
            case Hit::back: window.engine.backToTimeline (hit.track); break;
            case Hit::slotCell:
            case Hit::play:
                window.moveCursor (hit.track, hit.scene);
                window.engageTrack();
                // A clip is drawn in the Device Chain's place; an empty slot shows the devices.
                if (window.slots[(size_t) hit.track][(size_t) hit.scene] != nullptr)
                    window.showSlotClip (hit.track, hit.scene);
                else
                    window.showDevices();
                if (hit.kind == Hit::play)
                {
                    // The record dot: a take into the slot, on the armed track, or the take there stopped.
                    const auto take = window.engine.recordInfo (hit.track);
                    if (take.scene == hit.scene && take.state != mixer::RecordState::none)
                        window.recordPressed();
                    else if (window.slots[(size_t) hit.track][(size_t) hit.scene] == nullptr && hit.track == window.engine.liveTrack())
                        window.recordSlot (hit.track, hit.scene);
                    else
                        window.launchSlot (hit.track, hit.scene);
                }
                break;
            case Hit::layer:
                window.moveCursor (hit.track, hit.scene);
                if (window.engine.recordInfo (hit.track).state != mixer::RecordState::none)
                    window.recordPressed();   // the layer recording there: cancelled, or left to end
                else
                    window.recordLayer (hit.track, hit.scene, hit.audio);
                break;
            case Hit::sceneCell:
                window.moveCursor (window.selected < mixer::masterIndex ? window.selected : 0, hit.scene);
                if (e->position().x() < sceneRect (hit.scene).left() + playWidth)
                    launchScene (hit.scene);
                break;
            case Hit::stop: window.engine.stopTrack (hit.track); break;
            case Hit::stopAll: window.engine.stopAll(); break;
            case Hit::none: break;
        }
        if (! single)
            window.setFocus();
        update();
    }

    void mouseDoubleClickEvent (QMouseEvent* e) override
    {
        const auto hit = hitAt (e->position().toPoint());
        if (hit.kind == Hit::slotCell)
        {
            if (window.slots[(size_t) hit.track][(size_t) hit.scene] != nullptr)
                window.launchSlot (hit.track, hit.scene);
            else
                chooseFile (hit.track, hit.scene);
        }
        else if (hit.kind == Hit::sceneCell && e->position().x() >= sceneRect (hit.scene).left() + playWidth)
            window.renameScene (hit.scene);
        update();
    }

    void contextMenuEvent (QContextMenuEvent* e) override
    {
        const auto hit = hitAt (e->pos());
        QMenu menu (this);
        if (hit.kind == Hit::sceneCell)
        {
            menu.addAction (MixerWindow::tr ("&Launch Scene (Shift+Enter)"), this, [this, s = hit.scene] { launchScene (s); });
            menu.addAction (MixerWindow::tr ("&Rename Scene..."), this, [this, s = hit.scene] { window.renameScene (s); });
            menu.addSeparator();
            menu.addAction (MixerWindow::tr ("&cubify..."), this, [this, s = hit.scene] { window.cubifyScene (s); })
                ->setStatusTip (MixerWindow::tr ("The scene arranged by Xenakis's cubes: 24 sections onto its tracks"));
            // Reich's and Glass's processes on the scene, onto the timeline (mixer/Minimalize.h).
            menu.addAction (MixerWindow::tr ("minimalize..."), this, [this, s = hit.scene] { window.minimalizeScene (s); });
            menu.exec (e->globalPos());
            return;
        }
        if (hit.kind != Hit::slotCell && hit.kind != Hit::play)
            return;
        const auto t = hit.track, s = hit.scene;
        window.moveCursor (t, s);
        const auto slot = window.slots[(size_t) t][(size_t) s];
        if (slot != nullptr)
        {
            menu.addAction (MixerWindow::tr ("&Launch (Enter)"), this, [this, t, s] { window.launchSlot (t, s); });
            menu.addAction (MixerWindow::tr ("&Stop the Track (.)"), this, [this, t] { window.engine.stopTrack (t); });
            menu.addSeparator();
            auto* loop = menu.addAction (MixerWindow::tr ("L&oop"), this, [this, t, s] (bool on) { window.editSlot (t, s, [on] (mixer::Slot& x) { x.loop = on; }); });
            loop->setCheckable (true);
            loop->setChecked (slot->loop);
            auto* reverse = menu.addAction (MixerWindow::tr ("&Reverse"), this, [this, t, s] (bool on)
            {
                window.editSlot (t, s, [on] (mixer::Slot& x)
                {
                    if (x.clip.reversed == on)
                        return;
                    x.clip.reversed = on;
                    if (x.clip.midi != nullptr)
                        x.clip.midi = mixer::reverseMidi (*x.clip.midi);
                });
            });
            reverse->setCheckable (true);
            reverse->setChecked (slot->clip.reversed);
            auto* quantise = menu.addMenu (MixerWindow::tr ("Launch &Quantise"));
            auto* global = quantise->addAction (MixerWindow::tr ("The Launcher's"), this, [this, t, s] { window.editSlot (t, s, [] (mixer::Slot& x) { x.quantise = -1.0; }); });
            global->setCheckable (true);
            global->setChecked (slot->quantise < 0.0);
            for (int q = 0; q < numQuantise; ++q)
            {
                auto* a = quantise->addAction (quantiseName (q), this, [this, t, s, q] { window.editSlot (t, s, [q] (mixer::Slot& x) { x.quantise = quantiseBeats[q]; }); });
                a->setCheckable (true);
                a->setChecked (slot->quantise >= 0.0 && quantiseIndex (slot->quantise) == q);
            }
            menu.addAction (MixerWindow::tr ("Drums to &MIDI"), this, [this, t, s] { window.drumsToMidi (t, 0, s); })
                ->setEnabled (slot->clip.audio != nullptr && slot->clip.midi == nullptr);
            menu.addAction (MixerWindow::tr ("Send to &Timeline"), this, [this, t, s] { window.slotToTimeline (t, s); })
                ->setStatusTip (MixerWindow::tr ("A copy of the clip on the track's timeline at the playhead"));
            menu.addSeparator();
        }
        menu.addAction (MixerWindow::tr ("Choose &File..."), this, [this, t, s] { chooseFile (t, s); });
        menu.addAction (MixerWindow::tr ("&Empty the Slot (Delete)"), this, [this, t, s] { window.clearSlot (t, s); })->setEnabled (slot != nullptr);
        menu.exec (e->globalPos());
    }

    void dragEnterEvent (QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    void dragMoveEvent (QDragMoveEvent* e) override
    {
        const auto hit = hitAt (e->position().toPoint());
        if (hit.kind == Hit::slotCell || hit.kind == Hit::play)
            e->acceptProposedAction();
        else
            e->ignore();
    }

    // The first file into the slot it is dropped on, the others into the empty slots below.
    void dropEvent (QDropEvent* e) override
    {
        const auto hit = hitAt (e->position().toPoint());
        if (hit.kind != Hit::slotCell && hit.kind != Hit::play)
            return;
        auto scene = hit.scene;
        for (const auto& url : e->mimeData()->urls())
        {
            if (! url.isLocalFile() || ! QFileInfo (url.toLocalFile()).isFile())
                continue;
            if (single && window.audioIntoMidiTrack (hit.track, url.toLocalFile(), scene, 0))
            {
                e->acceptProposedAction();
                return;
            }
            if (scene < 0)
                break;
            // A file of the other kind on a clip of one kind joins it as its other layer.
            if (scene == hit.scene && window.addLayerFile (hit.track, scene, url.toLocalFile()))
            {
                scene = window.firstEmptyScene (hit.track);
                if (scene < 0 && window.numScenes < mixer::maxScenes)
                    scene = window.numScenes;
                continue;
            }
            window.loadSlot (hit.track, scene, url.toLocalFile());
            scene = window.firstEmptyScene (hit.track);
            if (scene < 0 && window.numScenes < mixer::maxScenes)
                scene = window.numScenes;
        }
        window.moveCursor (hit.track, hit.scene);
        e->acceptProposedAction();
    }

private:
    struct Hit
    {
        enum Kind { none, header, back, slotCell, play, layer, sceneCell, stop, stopAll } kind = none;
        int track = -1, scene = -1;
        bool audio = false;   // a layer's half: the audio's (below), else the MIDI's
    };

    static constexpr int margin = 4, headerHeight = 24, stopHeight = 24, sceneWidth = 130, minRow = 44, playWidth = 20;
    static constexpr int maxRow = 64, splitFrom = 24;   // a slot this tall or more shows its two layers
    // Columns keep one standard width (eight tracks fit in about 1100 px) rather than
    // stretching across the window; narrower when the room runs out.
    static constexpr int maxColumn = 150;
    static constexpr int singleColumn = 150;   // a slot's width in the editor's one row

    int firstTrack() const { return single ? window.editorTrack() : 0; }
    int trackCount() const { return single ? 1 : window.engine.numTracks(); }
    int columnWidth() const
    {
        if (single)
            return std::max (singleColumn, (width() - 2 * margin - sceneWidth) / std::max (1, window.numScenes));
        return std::clamp ((width() - 2 * margin - sceneWidth) / std::max (1, window.engine.numTracks()), 40, maxColumn);
    }
    int rowHeight() const
    {
        if (single)
            return std::clamp (height() - 2 * margin, minRow, 400);
        return std::clamp ((height() - 2 * margin - headerHeight - stopHeight) / std::max (1, window.numScenes), minRow, maxRow);
    }
    bool splitSlots() const { return rowHeight() - 4 >= splitFrom; }
    // A slot's halves: the MIDI layer on top, the audio below.
    QRectF halfRect (int t, int s, bool audio) const
    {
        const QRectF body = QRectF (slotRect (t, s)).adjusted (playWidth, 2, -2, -4);
        const auto h = body.height() / 2.0;
        return { body.left(), audio ? body.top() + h : body.top(), body.width(), h };
    }
    QRectF layerDotRect (int t, int s, bool audio) const
    {
        const auto half = halfRect (t, s, audio);
        return { half.right() - half.height(), half.top(), half.height(), half.height() };
    }
    // The scenes on the left (owner, 2026-10-25), the tracks to their right.
    // In the editor's one row the track's name and stop are where the scenes would be.
    int trackLeft() const { return margin + sceneWidth; }
    QRect headerRect (int t) const
    {
        if (single)
            return { margin + 1, margin + 2, sceneWidth - 6, headerHeight - 3 };
        return { trackLeft() + t * columnWidth() + 1, margin, columnWidth() - 2, headerHeight - 3 };
    }
    QRect backRect (int t) const { const auto h = headerRect (t); return { h.right() - 22, h.top(), 22, h.height() }; }
    QRect slotRect (int t, int s) const
    {
        if (single)
            return QRect (trackLeft() + s * columnWidth(), margin, columnWidth(), rowHeight()).adjusted (2, 2, -2, -2);
        return QRect (trackLeft() + t * columnWidth(), margin + headerHeight + s * rowHeight(), columnWidth(), rowHeight()).adjusted (2, 2, -2, -2);
    }
    QRect stopRect (int t) const
    {
        if (single)
            return QRect (margin, margin + headerHeight, sceneWidth - 4, stopHeight).adjusted (1, 3, -1, -1);
        return QRect (trackLeft() + t * columnWidth(), margin + headerHeight + window.numScenes * rowHeight(), columnWidth(), stopHeight).adjusted (2, 3, -2, -1);
    }
    int sceneLeft() const { return margin; }
    QRect sceneHeaderRect() const { return single ? QRect() : QRect (sceneLeft(), margin, sceneWidth, headerHeight - 3); }
    QRect sceneRect (int s) const
    {
        if (single)
            return {};
        return QRect (sceneLeft(), margin + headerHeight + s * rowHeight(), sceneWidth, rowHeight()).adjusted (2, 2, -4, -2);
    }
    QRect stopAllRect() const
    {
        if (single)
            return {};
        return QRect (sceneLeft(), margin + headerHeight + window.numScenes * rowHeight(), sceneWidth, stopHeight).adjusted (2, 3, -4, -1);
    }

    Hit hitAt (QPoint at) const
    {
        Hit hit;
        for (int t = firstTrack(); t < firstTrack() + trackCount(); ++t)
        {
            if (backRect (t).contains (at) && ! window.engine.launchInfo (t).timeline)
                return { Hit::back, t, -1 };
            if (headerRect (t).contains (at))
                return { Hit::header, t, -1 };
            if (stopRect (t).contains (at))
                return { Hit::stop, t, -1 };
            for (int s = 0; s < window.numScenes; ++s)
                if (const auto r = slotRect (t, s); r.contains (at))
                {
                    const auto& slot = window.slots[(size_t) t][(size_t) s];
                    if (slot != nullptr && splitSlots() && t == window.engine.liveTrack())
                        for (const bool audio : { false, true })
                            if (layerDotRect (t, s, audio).contains (at)
                                && ((audio ? slot->clip.audio == nullptr : slot->clip.midi == nullptr)
                                    || (window.layerTakes[(size_t) t].scene == s && window.layerTakes[(size_t) t].audio == audio)))
                                return { Hit::layer, t, s, audio };
                    const bool dot = slot != nullptr || t == window.engine.liveTrack() || window.engine.recordInfo (t).scene == s;
                    return { at.x() < r.left() + playWidth && dot ? Hit::play : Hit::slotCell, t, s };
                }
        }
        for (int s = 0; s < window.numScenes; ++s)
            if (sceneRect (s).contains (at))
                return { Hit::sceneCell, -1, s };
        if (stopAllRect().contains (at))
            hit.kind = Hit::stopAll;
        return hit;
    }

    // A layer's picture, small: the notes by pitch, the sound's peaks.
    static void drawNotes (QPainter& p, const QRectF& r, const mixer::Clip& c, const QColor& ink)
    {
        if (c.midi == nullptr || c.length <= 0)
            return;
        const auto& events = c.midi->events;
        int low = 127, high = 0;
        for (const auto& e : events)
            if (((e.status & 0xF0) == 0x90 && e.data2 > 0) && e.frame >= c.offset && e.frame < c.offset + c.length)
            {
                low = std::min (low, (int) e.data1);
                high = std::max (high, (int) e.data1);
            }
        if (low > high)
            return;
        const auto span = std::max (1, high - low);
        const auto x = [&] (Frame f) { return r.left() + r.width() * (double) std::clamp<Frame> (f - c.offset, 0, c.length) / (double) c.length; };
        const auto lane = std::max (1.0, std::min (3.0, r.height() / (span + 1)));
        p.setPen (Qt::NoPen);
        p.setBrush (ink);
        std::array<std::array<Frame, 128>, 16> on;
        for (auto& ch : on)
            ch.fill (-1);
        for (const auto& e : events)
        {
            const auto ch = (size_t) (e.status & 0x0F), key = (size_t) (e.data1 & 0x7F);
            if (((e.status & 0xF0) == 0x90 && e.data2 > 0))
                on[ch][key] = e.frame;
            else if (((e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0)) && on[ch][key] >= 0)
            {
                const auto y = r.bottom() - lane - (r.height() - lane) * (double) ((int) key - low) / (double) span;
                const auto from = x (on[ch][key]), to = x (e.frame);
                if (to > from)
                    p.drawRect (QRectF (from, y, std::max (1.0, to - from), lane));
                on[ch][key] = -1;
            }
        }
    }

    static void drawWave (QPainter& p, const QRectF& r, const mixer::Clip& c, const QColor& ink)
    {
        if (c.audio == nullptr || c.length <= 0 || r.width() < 2)
            return;
        const auto& a = *c.audio;
        const auto last = a.length() - 1;
        const auto columns = (int) r.width();
        const auto mid = r.center().y();
        p.setPen (QPen (ink, 1));
        for (int k = 0; k < columns; ++k)
        {
            const auto from = (Frame) ((double) c.length * k / columns), to = (Frame) ((double) c.length * (k + 1) / columns);
            const auto step = std::max<Frame> (1, (to - from) / 24);   // a few samples a column are enough to see
            float peak = 0.0f;
            for (auto f = from; f < to; f += step)
            {
                const auto src = (size_t) std::clamp<Frame> (c.reversed ? last - (c.offset + f) : c.offset + f, 0, last);
                peak = std::max ({ peak, std::abs (a.left[src]), std::abs (a.right[src]) });
            }
            const auto h = std::max (0.5, std::min (1.0, (double) peak) * r.height() * 0.5);
            p.drawLine (QPointF (r.left() + k + 0.5, mid - h), QPointF (r.left() + k + 0.5, mid + h));
        }
    }

    void launchScene (int scene)
    {
        const bool wasPlaying = window.engine.isPlaying();
        window.engine.launchScene (scene);
        if (! wasPlaying)
            Q_EMIT window.playStarted();
    }

    void chooseFile (int t, int s)
    {
        const auto file = QFileDialog::getOpenFileName (this, MixerWindow::tr ("File for %1, %2").arg (window.trackName (t), window.sceneName (s)), window.folder(),
                                                        MixerWindow::tr ("Audio and MIDI files (*.wav *.flac *.aif *.aiff *.mp3 *.ogg *.opus *.m4a *.mid *.midi);;All files (*)"));
        if (! file.isEmpty())
            window.loadSlot (t, s, file);
        if (! single)
            window.setFocus();
    }

    MixerWindow& window;
    bool single = false;
};

// The selected track's devices, on the launcher's left: its instrument, the track's own
// settings, and the clip in the slot under the cursor.
class LauncherRack final : public QFrame
{
public:
    explicit LauncherRack (MixerWindow& w) : QFrame (&w), window (w)
    {
        setFrameShape (QFrame::StyledPanel);
        setMinimumWidth (210);
        setMaximumWidth (280);
        setFocusPolicy (Qt::NoFocus);
        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (6, 6, 6, 6);
        layout->setSpacing (6);

        colourBar = new QFrame (this);
        colourBar->setFixedHeight (5);
        title = new QLabel (this);
        auto bold = title->font();
        bold.setBold (true);
        title->setFont (bold);
        layout->addWidget (colourBar);
        layout->addWidget (title);

        auto noFocus = [] (QWidget* widget) { widget->setFocusPolicy (Qt::NoFocus); return widget; };
        auto button = [this, &noFocus] (const QString& text, const QString& tip, bool checkable)
        {
            auto* b = new QPushButton (text, this);
            noFocus (b);
            b->setCheckable (checkable);
            b->setToolTip (tip);
            return b;
        };

        // The instrument.
        instrumentGroup = new QGroupBox (MixerWindow::tr ("Instrument"), this);
        auto* instrumentLayout = new QVBoxLayout (instrumentGroup);
        instrumentBox = new QComboBox (instrumentGroup);
        noFocus (instrumentBox);
        instrumentBox->setToolTip (MixerWindow::tr ("What the track's MIDI clips and the MIDI keyboard play"));
        editButton = button (MixerWindow::tr ("Edit"), MixerWindow::tr ("Open the instrument's window (E)"), false);
        armButton = button (MixerWindow::tr ("● Keys"), MixerWindow::tr ("The MIDI keyboard plays this track's instrument (R)"), true);
        auto* instrumentRow = new QHBoxLayout;
        instrumentRow->addWidget (editButton);
        instrumentRow->addWidget (armButton);
        instrumentLayout->addWidget (instrumentBox);
        instrumentLayout->addLayout (instrumentRow);
        layout->addWidget (instrumentGroup);

        // The track.
        trackGroup = new QGroupBox (MixerWindow::tr ("Track"), this);
        auto* form = new QFormLayout (trackGroup);
        trimBox = new QDoubleSpinBox (trackGroup);
        trimBox->setRange (-24.0, 24.0);
        trimBox->setSingleStep (0.5);
        trimBox->setDecimals (1);
        trimBox->setSuffix (MixerWindow::tr (" dB"));
        trimBox->setToolTip (MixerWindow::tr ("Trim, before everything else"));
        transposeBox = new QSpinBox (trackGroup);
        transposeBox->setRange (-48, 48);
        transposeBox->setSuffix (MixerWindow::tr (" st"));
        transposeBox->setToolTip (MixerWindow::tr ("Transpose the MIDI clips (channel 10, the drums, stays)"));
        channelsButton = button (MixerWindow::tr ("Channels..."), MixerWindow::tr ("Which of the MIDI clips' channels play"), false);
        invertL = button (QStringLiteral ("ØL"), MixerWindow::tr ("Flip the left side's polarity"), true);
        invertR = button (QStringLiteral ("ØR"), MixerWindow::tr ("Flip the right side's polarity"), true);
        auto* polarity = new QHBoxLayout;
        polarity->addWidget (invertL);
        polarity->addWidget (invertR);
        followLabel = new QLabel (trackGroup);
        followLabel->setWordWrap (true);
        backButton = button (MixerWindow::tr ("Back to Timeline (T)"), MixerWindow::tr ("The track plays its timeline clips again; Shift+T does it for every track"), false);
        form->addRow (MixerWindow::tr ("Trim"), trimBox);
        form->addRow (MixerWindow::tr ("Transpose"), transposeBox);
        form->addRow (MixerWindow::tr ("MIDI"), channelsButton);
        form->addRow (MixerWindow::tr ("Polarity"), polarity);
        form->addRow (followLabel);
        form->addRow (backButton);
        layout->addWidget (trackGroup);
        // In the Inspector the strip above has the instrument, trim, transpose, MIDI channels and
        // polarity; the rack keeps the launcher's part: the timeline and the clip.
        instrumentGroup->hide();
        colourBar->hide();
        title->hide();
        for (auto* row : std::initializer_list<QWidget*> { trimBox, transposeBox, channelsButton })
            form->setRowVisible (row, false);
        form->setRowVisible (polarity, false);
        trackGroup->setTitle (MixerWindow::tr ("Launcher"));
        setFrameShape (QFrame::NoFrame);
        setMaximumWidth (QWIDGETSIZE_MAX);

        // The clip under the cursor.
        clipGroup = new QGroupBox (MixerWindow::tr ("Clip"), this);
        auto* clipLayout = new QFormLayout (clipGroup);
        clipName = new QLabel (clipGroup);
        clipName->setWordWrap (true);
        loopBox = new QCheckBox (MixerWindow::tr ("Loop"), clipGroup);
        noFocus (loopBox);
        loopBox->setToolTip (MixerWindow::tr ("Go round until stopped; off, it plays once and the track stops"));
        reverseBox = new QCheckBox (MixerWindow::tr ("Reverse"), clipGroup);
        noFocus (reverseBox);
        quantiseBox = new QComboBox (clipGroup);
        noFocus (quantiseBox);
        quantiseBox->addItem (MixerWindow::tr ("The launcher's"));
        for (int q = 0; q < numQuantise; ++q)
            quantiseBox->addItem (quantiseName (q));
        quantiseBox->setToolTip (MixerWindow::tr ("When a launch of this clip starts: the launcher's quantise, or its own"));
        chooseButton = button (MixerWindow::tr ("File..."), MixerWindow::tr ("Put a file in this slot (or drop one on it)"), false);
        clearButton = button (MixerWindow::tr ("Empty"), MixerWindow::tr ("Empty the slot (Delete)"), false);
        auto* fileRow = new QHBoxLayout;
        fileRow->addWidget (chooseButton);
        fileRow->addWidget (clearButton);
        clipLayout->addRow (clipName);
        clipLayout->addRow (loopBox);
        clipLayout->addRow (reverseBox);
        clipLayout->addRow (MixerWindow::tr ("Quantise"), quantiseBox);
        clipLayout->addRow (fileRow);
        layout->addWidget (clipGroup);

        // The tuning the PHI synth plays in: the launched clip's, else its scene's, else the track's.
        tuningGroup = new QGroupBox (MixerWindow::tr ("Tuning (PHI synth)"), this);
        tuningGroup->setToolTip (MixerWindow::tr ("Every mode and tuning of the atlas: ragas in just intonation, church modes, Greek genera, maqams, makams "
                                                  "and dastgahs with their quarter steps, gamelan laras, temperaments, Partch's 43 and more, or 2 to 96 equal steps. "
                                                  "The PHI synth plays it; other instruments stay in 12-TET."));
        auto* tuningLayout = new QGridLayout (tuningGroup);
        tuningLayout->setColumnStretch (1, 1);
        auto tuningRow = [&] (TuningRow& row, const QString& label, const QString& none, const QString& tip)
        {
            row.system = new QComboBox (tuningGroup);
            row.root = new QComboBox (tuningGroup);
            noFocus (row.system);
            noFocus (row.root);
            row.system->setToolTip (tip);
            row.root->setToolTip (MixerWindow::tr ("The tonic: Sa, the final, or the maqam's root"));
            row.system->addItem (none, -1);
            std::string family;
            const auto& all = mixer::tuningSystems();
            for (size_t i = 0; i < all.size(); ++i)
            {
                if (all[i].family != family)
                {
                    family = all[i].family;
                    row.system->insertSeparator (row.system->count());
                }
                row.system->addItem (QString::fromStdString (all[i].name), (int) i);
                row.system->setItemData (row.system->count() - 1, QString::fromStdString (all[i].family), Qt::ToolTipRole);
            }
            row.system->setMaxVisibleItems (24);
            for (const auto* name : rootNames)
                row.root->addItem (QString::fromUtf8 (name));
            row.root->setMaximumWidth (56);
            const auto at = tuningLayout->rowCount();
            tuningLayout->addWidget (new QLabel (label, tuningGroup), at, 0);
            tuningLayout->addWidget (row.system, at, 1);
            tuningLayout->addWidget (row.root, at, 2);
            // The atlas (ModeAtlas.h): every mode and tuning by region, people and culture.
            row.browse = new QToolButton (tuningGroup);
            row.browse->setText (QStringLiteral ("…"));
            row.browse->setToolTip (MixerWindow::tr ("Choose from the atlas: by region, then by tradition, composer or theorist; or search"));
            noFocus (row.browse);
            tuningLayout->addWidget (row.browse, at, 3);
        };
        tuningRow (trackTuning, MixerWindow::tr ("Track"), MixerWindow::tr ("12-TET"), MixerWindow::tr ("The track's tuning, when neither its clip nor the scene has one"));
        tuningRow (sceneTuning, MixerWindow::tr ("Scene"), MixerWindow::tr ("The track's"), MixerWindow::tr ("The tuning for every clip in the scene under the cursor"));
        tuningRow (clipTuning, MixerWindow::tr ("Clip"), MixerWindow::tr ("The scene's"), MixerWindow::tr ("This clip's own tuning, over the scene's and the track's"));
        tuningLabel = new QLabel (tuningGroup);
        tuningLabel->setWordWrap (true);
        tuningLayout->addWidget (tuningLabel, tuningLayout->rowCount(), 0, 1, 4);
        layout->addWidget (tuningGroup);
        layout->addStretch (1);

        auto track = [this] { return window.selected < mixer::masterIndex ? window.selected : -1; };
        auto stripChanged = [this] (int t)
        {
            window.strips[(size_t) t]->refresh();
            window.changed();
        };
        QObject::connect (instrumentBox, &QComboBox::activated, this, [this, track] (int choice)
        {
            if (const auto t = track(); t >= 0)
            {
                window.setInstrumentKind (t, std::min (choice, (int) firstPlugin), instrumentBox->itemData (choice).toString());
                window.strips[(size_t) t]->refresh();
                refresh();
            }
            window.setFocus();
        });
        QObject::connect (editButton, &QPushButton::clicked, this, [this, track] { if (track() >= 0) window.editInstrument (track()); });
        QObject::connect (armButton, &QPushButton::clicked, this, [this, track]
        {
            if (track() >= 0)
                window.arm (track());
            refresh();
        });
        QObject::connect (trimBox, &QDoubleSpinBox::valueChanged, this, [this, track, stripChanged] (double v)
        {
            if (updating || track() < 0)
                return;
            window.engine.strip (track()).trimDb = (float) v;
            stripChanged (track());
        });
        QObject::connect (transposeBox, &QSpinBox::valueChanged, this, [this, track, stripChanged] (int v)
        {
            if (updating || track() < 0)
                return;
            window.engine.strip (track()).transpose = v;
            stripChanged (track());
        });
        for (auto* box : std::initializer_list<QAbstractSpinBox*> { trimBox, transposeBox })
            QObject::connect (box, &QAbstractSpinBox::editingFinished, &window, [this] { window.setFocus(); });
        QObject::connect (channelsButton, &QPushButton::clicked, this, [this, track]
        {
            if (track() >= 0)
                window.strips[(size_t) track()]->channelMenu (channelsButton->mapToGlobal (QPoint (0, channelsButton->height())));
        });
        QObject::connect (invertL, &QPushButton::toggled, this, [this, track, stripChanged] (bool on)
        {
            if (! updating && track() >= 0)
            {
                window.engine.strip (track()).invertLeft = on;
                stripChanged (track());
            }
        });
        QObject::connect (invertR, &QPushButton::toggled, this, [this, track, stripChanged] (bool on)
        {
            if (! updating && track() >= 0)
            {
                window.engine.strip (track()).invertRight = on;
                stripChanged (track());
            }
        });
        QObject::connect (backButton, &QPushButton::clicked, this, [this, track] { if (track() >= 0) window.engine.backToTimeline (track()); });
        QObject::connect (loopBox, &QCheckBox::toggled, this, [this, track] (bool on)
        {
            if (! updating && track() >= 0)
                window.editSlot (track(), window.cursorScene, [on] (mixer::Slot& x) { x.loop = on; });
        });
        QObject::connect (reverseBox, &QCheckBox::toggled, this, [this, track] (bool on)
        {
            if (updating || track() < 0)
                return;
            window.editSlot (track(), window.cursorScene, [on] (mixer::Slot& x)
            {
                if (x.clip.reversed == on)
                    return;
                x.clip.reversed = on;
                if (x.clip.midi != nullptr)
                    x.clip.midi = mixer::reverseMidi (*x.clip.midi);
            });
        });
        QObject::connect (quantiseBox, &QComboBox::activated, this, [this, track] (int choice)
        {
            if (track() >= 0)
                window.editSlot (track(), window.cursorScene, [choice] (mixer::Slot& x) { x.quantise = choice == 0 ? -1.0 : quantiseBeats[choice - 1]; });
            window.setFocus();
        });
        QObject::connect (chooseButton, &QPushButton::clicked, this, [this, track]
        {
            if (track() < 0)
                return;
            const auto file = QFileDialog::getOpenFileName (this, MixerWindow::tr ("File for %1, %2").arg (window.trackName (track()), window.sceneName (window.cursorScene)),
                                                            window.folder(),
                                                            MixerWindow::tr ("Audio and MIDI files (*.wav *.flac *.aif *.aiff *.mp3 *.ogg *.opus *.m4a *.mid *.midi);;All files (*)"));
            if (! file.isEmpty())
                window.loadSlot (track(), window.cursorScene, file);
            window.setFocus();
        });
        QObject::connect (clearButton, &QPushButton::clicked, this, [this, track] { if (track() >= 0) window.clearSlot (track(), window.cursorScene); });
        auto tuningEdited = [this, track] (TuningRow& row, int level)
        {
            return [this, track, &row, level]
            {
                if (updating || track() < 0)
                    return;
                const auto choice = row.choice();
                if (level == 0)
                    window.info[(size_t) track()].tuning = choice;
                else if (level == 1)
                    window.sceneTunings[(size_t) window.cursorScene] = choice;
                else
                    window.editSlot (track(), window.cursorScene, [choice] (mixer::Slot& x) { x.tuning = choice; });
                if (level != 2)
                    window.changed();
                window.applyTunings();
                refresh();
                window.setFocus();
            };
        };
        for (auto [row, level] : { std::pair { &trackTuning, 0 }, { &sceneTuning, 1 }, { &clipTuning, 2 } })
        {
            QObject::connect (row->system, &QComboBox::activated, this, tuningEdited (*row, level));
            QObject::connect (row->root, &QComboBox::activated, this, tuningEdited (*row, level));
            QObject::connect (row->browse, &QToolButton::clicked, this, [this, row, edited = tuningEdited (*row, level)]
            {
                auto pick = [row, edited] (int i)
                {
                    row->system->setCurrentIndex (std::max (0, row->system->findData (i)));
                    row->root->setEnabled (true);
                    edited();
                };
                QMenu menu (this);
                modeatlas::fill (&menu, mixer::melodic | mixer::tuning, row->system->currentData().toInt(), pick);
                menu.addSeparator();
                menu.addAction (MixerWindow::tr ("find..."), this, [this, pick]
                {
                    if (const auto i = modeatlas::find (this, mixer::melodic | mixer::tuning, MixerWindow::tr ("Find a tuning")); i >= 0)
                        pick (i);
                });
                menu.exec (row->browse->mapToGlobal (QPoint (0, row->browse->height())));
            });
        }
    }

    // Everything shown, for the selected track and the slot under the cursor.
    void refresh()
    {
        updating = true;
        const auto t = window.selected < mixer::masterIndex ? window.selected : -1;
        for (auto* group : { instrumentGroup, trackGroup, clipGroup, tuningGroup })
            group->setEnabled (t >= 0);
        if (t < 0)
        {
            title->setText (MixerWindow::tr ("Master"));
            colourBar->setStyleSheet ({});
            clipName->setText ({});
            updating = false;
            return;
        }
        const auto& info = window.info[(size_t) t];
        colourBar->setStyleSheet (QStringLiteral ("background: %1;").arg (info.colour.name()));
        title->setText (QStringLiteral ("%1  %2").arg (t + 1).arg (info.name));
        const auto inst = MixerStrip::fillInstruments (instrumentBox, window, t);
        editButton->setEnabled (inst != nullptr && (inst->kind() != mixer::Instrument::Kind::plugin || inst->hasWindow()));
        armButton->setChecked (window.engine.liveTrack() == t);
        const auto& s = window.engine.strip (t);
        trimBox->setValue (s.trimDb.load());
        transposeBox->setValue (s.transpose.load());
        invertL->setChecked (s.invertLeft.load());
        invertR->setChecked (s.invertRight.load());

        const auto& slot = window.slots[(size_t) t][(size_t) window.cursorScene];
        clipGroup->setTitle (MixerWindow::tr ("Clip: %1").arg (window.sceneName (window.cursorScene)));
        for (auto* widget : std::initializer_list<QWidget*> { loopBox, reverseBox, quantiseBox, clearButton })
            widget->setEnabled (slot != nullptr);
        if (slot != nullptr)
        {
            const auto& c = slot->clip;
            clipName->setText (MixerWindow::tr ("%1\n%2, %3 s").arg (window.slotName (t, window.cursorScene))
                                   .arg (c.isMidi() ? MixerWindow::tr ("MIDI") : MixerWindow::tr ("audio"))
                                   .arg ((double) c.length / window.rate(), 0, 'f', 2));
            loopBox->setChecked (slot->loop);
            reverseBox->setChecked (c.reversed);
            quantiseBox->setCurrentIndex (slot->quantise < 0.0 ? 0 : quantiseIndex (slot->quantise) + 1);
        }
        else
            clipName->setText (MixerWindow::tr ("An empty slot: drop a file on it, double-click it, or press Enter on a sound in the Find panel's Selector."));
        trackTuning.show (info.tuning);
        sceneTuning.show (window.sceneTunings[(size_t) window.cursorScene]);
        clipTuning.show (slot != nullptr ? slot->tuning : mixer::TuningChoice {});
        clipTuning.system->setEnabled (slot != nullptr);
        if (slot == nullptr)
            clipTuning.root->setEnabled (false);
        tick();
        updating = false;
    }

    void tick()
    {
        const auto t = window.selected < mixer::masterIndex ? window.selected : -1;
        if (t < 0)
            return;
        const auto launch = window.engine.launchInfo (t);
        followLabel->setText (launch.timeline ? MixerWindow::tr ("Plays its timeline clips.")
                              : launch.playing >= 0 ? MixerWindow::tr ("Plays %1 from the launcher.").arg (window.sceneName (launch.playing))
                                                    : MixerWindow::tr ("Stopped in the launcher; its timeline is not heard."));
        backButton->setEnabled (! launch.timeline);
        tuningLabel->setText (MixerWindow::tr ("Plays in %1.").arg (window.tuningText (window.tuningInForce (t))));
    }

private:
    struct TuningRow
    {
        QComboBox* system = nullptr;
        QComboBox* root = nullptr;
        QToolButton* browse = nullptr;
        mixer::TuningChoice choice() const { return { system->currentData().toInt(), root->currentIndex() }; }
        void show (const mixer::TuningChoice& c)
        {
            system->setCurrentIndex (std::max (0, system->findData (c.system)));
            root->setCurrentIndex (c.root);
            root->setEnabled (c.system >= 0);
        }
    };
    MixerWindow& window;
    bool updating = false;
    QFrame* colourBar;
    QLabel* title;
    QGroupBox* instrumentGroup;
    QComboBox* instrumentBox;
    QPushButton* editButton;
    QPushButton* armButton;
    QGroupBox* trackGroup;
    QDoubleSpinBox* trimBox;
    QSpinBox* transposeBox;
    QPushButton* channelsButton;
    QPushButton* invertL;
    QPushButton* invertR;
    QLabel* followLabel;
    QPushButton* backButton;
    QGroupBox* clipGroup;
    QLabel* clipName;
    QCheckBox* loopBox;
    QCheckBox* reverseBox;
    QComboBox* quantiseBox;
    QPushButton* chooseButton;
    QPushButton* clearButton;
    QGroupBox* tuningGroup;
    TuningRow trackTuning, sceneTuning, clipTuning;
    QLabel* tuningLabel;
};

//==============================================================================
//==============================================================================
// A return strip (Ableton): what the tracks' A or B sends feed, through one effect
// (fully wet), then pan, mute, a fader and a meter. Between the tracks and the master.
class ReturnStripWidget final : public QFrame
{
public:
    ReturnStripWidget (MixerWindow& w, int i) : QFrame (&w), window (w), index (i)
    {
        setFrameShape (QFrame::StyledPanel);
        setObjectName (QStringLiteral ("returnStrip"));
        setFocusPolicy (Qt::NoFocus);
        setMinimumWidth (96);
        setMaximumWidth (120);
        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (5, 5, 5, 5);
        layout->setSpacing (3);
        auto* title = new QLabel (index == 0 ? MixerWindow::tr ("A Reverb") : MixerWindow::tr ("B Delay"), this);
        auto bold = title->font();
        bold.setBold (true);
        title->setFont (bold);
        title->setAlignment (Qt::AlignCenter);
        layout->addWidget (title);
        effectButton = new QPushButton (this);
        effectButton->setFocusPolicy (Qt::NoFocus);
        effectButton->setContextMenuPolicy (Qt::CustomContextMenu);
        effectButton->setToolTip (MixerWindow::tr ("The return's effect, fully wet: click opens it, right-click picks another"));
        layout->addWidget (effectButton);
        panDial = new Potentiometer (this);
        panDial->setRange (-100, 100);
        panDial->setFixedSize (knob, knob);
        panDial->setFocusPolicy (Qt::NoFocus);
        static_cast<Potentiometer*> (panDial)->setOrigin (0);
        panDial->setToolTip (MixerWindow::tr ("The return's pan (balance; double-click: centre)"));
        panLabel = new QLabel (this);
        layout->addWidget (panDial, 0, Qt::AlignHCenter);
        layout->addWidget (panLabel, 0, Qt::AlignHCenter);
        muteButton = new QPushButton (MixerWindow::tr ("M"), this);
        muteButton->setCheckable (true);
        muteButton->setFocusPolicy (Qt::NoFocus);
        muteButton->setToolTip (MixerWindow::tr ("Mute the return"));
        layout->addWidget (muteButton);
        fader = new Fader (this);
        fader->setToolTip (MixerWindow::tr ("The return's level, -∞ to +6 dB"));
        meter = new LevelMeter (this);
        auto* row = new QHBoxLayout;
        row->addStretch (1);
        row->addWidget (fader);
        row->addWidget (meter);
        row->addStretch (1);
        layout->addLayout (row, 1);
        value = new QLabel (this);
        value->setAlignment (Qt::AlignCenter);
        layout->addWidget (value);

        QObject::connect (effectButton, &QPushButton::clicked, this, [this] { window.editReturn (index); });
        QObject::connect (effectButton, &QWidget::customContextMenuRequested, this, [this]
        {
            window.chooseReturnEffect (index, effectButton->mapToGlobal (QPoint (0, effectButton->height())));
        });
        QObject::connect (panDial, &QDial::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            controls().pan = (float) v / 100.0f;
            refresh();
            window.changed();
        });
        QObject::connect (muteButton, &QPushButton::toggled, this, [this] (bool on) { if (! updating) { controls().mute = on; window.changed(); } });
        QObject::connect (fader, &QSlider::valueChanged, this, [this] (int v)
        {
            if (updating)
                return;
            controls().faderDb = faderDb (v);
            refresh();
            window.changed();
        });
        panDial->installEventFilter (this);
    }

    mixer::ReturnStrip& controls() { return window.engine.returnStrip (index); }

    void refresh()
    {
        updating = true;
        auto& c = controls();
        const auto fx = window.engine.returnEffect (index);
        effectButton->setText (fx != nullptr ? QString::fromStdString (fx->name()) : MixerWindow::tr ("No effect"));
        panDial->setValue ((int) std::lround (c.pan.load() * 100.0f));
        panLabel->setText (panText (c.pan.load()));
        muteButton->setChecked (c.mute.load());
        fader->setValue (faderPosition (c.faderDb.load()));
        value->setText (dbText (c.faderDb.load()));
        updating = false;
    }

    void tick (double seconds) { meter->setLevels (window.engine.takeMeter (mixer::returnIndex + index), seconds); }

protected:
    bool eventFilter (QObject* o, QEvent* e) override
    {
        if (o == panDial && e->type() == QEvent::MouseButtonDblClick)
        {
            panDial->setValue (0);
            return true;
        }
        return QFrame::eventFilter (o, e);
    }

private:
    MixerWindow& window;
    const int index;
    bool updating = false;
    QPushButton* effectButton;
    QDial* panDial;
    QLabel* panLabel;
    QPushButton* muteButton;
    QSlider* fader;
    LevelMeter* meter;
    QLabel* value;
};

//==============================================================================
// The Device Chain (owner, 2026-10-25), along the mixer's bottom as in Ableton: the selected
// track's instrument, always first on the left, then its inserts left to right, each with up
// to eight knobs and its Dry/Wet, then a card that adds one. An instrument or effect dropped
// here (or on a strip) goes to its place. A clip picked in the launcher or the tracks puts the
// ClipView here instead; the Master strip hides it for the editor's own chain.
class DeviceTile final : public QFrame
{
public:
    DeviceTile (QWidget* parent, const QColor& c, bool add = false) : QFrame (parent), colour (c), adding (add)
    {
        setAttribute (Qt::WA_StyledBackground, false);
        auto* outer = new QVBoxLayout (this);
        outer->setContentsMargins (8, 8, 8, 6);
        outer->setSpacing (4);
        head = new QHBoxLayout;
        head->setSpacing (4);
        outer->addLayout (head);
        body = new QGridLayout;
        body->setHorizontalSpacing (2);
        body->setVerticalSpacing (0);
        outer->addLayout (body);
        outer->addStretch (1);
    }

    QHBoxLayout* head;
    QGridLayout* body;

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::Antialiasing);
        const auto r = QRectF (rect()).adjusted (1.5, 1.5, -1.5, -1.5);
        g.setPen (QPen (palette().color (QPalette::Mid), 1.0));
        g.setBrush (palette().color (adding ? QPalette::Window : QPalette::Base));
        g.drawRoundedRect (r, 6, 6);
        if (! adding)
        {
            // The track's colour along the top, as on its strip's name.
            g.setPen (Qt::NoPen);
            g.setBrush (colour);
            g.drawRoundedRect (QRectF (r.left() + 6, r.top() + 1, r.width() - 12, 3), 1.5, 1.5);
        }
    }

private:
    QColor colour;
    bool adding;
};

class TrackDevices final : public QWidget
{
public:
    explicit TrackDevices (MixerWindow& w) : QWidget (&w), window (w)
    {
        setAcceptDrops (true);
        setAccessibleName (MixerWindow::tr ("Device Chain"));
        auto* outer = new QHBoxLayout (this);
        outer->setContentsMargins (0, 0, 0, 0);
        scroll = new QScrollArea (this);
        scroll->setWidgetResizable (true);
        scroll->setFrameShape (QFrame::NoFrame);
        scroll->setFocusPolicy (Qt::NoFocus);
        scroll->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
        scroll->viewport()->setAutoFillBackground (false);
        row = new QWidget;
        row->setAutoFillBackground (false);
        rowLayout = new QHBoxLayout (row);
        rowLayout->setContentsMargins (6, 4, 6, 4);
        rowLayout->setSpacing (6);
        scroll->setWidget (row);
        row->setAutoFillBackground (false);   // setWidget turns it on; see-through like the strips
        outer->addWidget (scroll);
        setMinimumHeight (120);
    }

    // Rebuilt when the track's devices changed, else their knobs follow the engine.
    void refresh()
    {
        const auto track = window.selected < mixer::masterIndex ? std::min (window.selected, window.engine.numTracks() - 1) : -1;
        QString sig = QString::number (track);
        if (track >= 0)
        {
            sig += QLatin1Char (':') + QString::number ((quintptr) window.engine.instrument (track).get());
            for (const auto& x : window.engine.inserts (track))
                sig += QLatin1Char (',') + QString::number ((quintptr) x.effect.get());
            sig += QLatin1Char (':') + QString::number (window.engine.strip (track).mode.load()) + window.info[(size_t) track].colour.name();
        }
        if (sig != signature)
        {
            signature = sig;
            rebuild (track);
        }
        sync();
    }

protected:
    void dragEnterEvent (QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat (MixerWindow::pluginMime()) && window.selected < mixer::masterIndex)
            e->acceptProposedAction();
    }

    void dropEvent (QDropEvent* e) override
    {
        if (window.dropPlugin (window.selected, e->mimeData()->data (MixerWindow::pluginMime())))
            e->acceptProposedAction();
    }

private:
    struct Knob
    {
        int insert = -1;
        int parameter = -1;   // the effect's; -1 its Dry/Wet
        int steps = 0;
        QDial* dial = nullptr;
        QLabel* value = nullptr;
    };

    static constexpr int knobsPerRow = 5, maxKnobs = 8, knobSize = 30, labelWidth = 50, resolution = 1000;

    void clear()
    {
        knobs.clear();
        powers.clear();
        while (auto* item = rowLayout->takeAt (0))
        {
            if (auto* w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    QLabel* smallLabel (const QString& text, QWidget* parent)
    {
        auto* l = new QLabel (text, parent);
        auto f = l->font();
        f.setPointSizeF (f.pointSizeF() * 0.85);
        l->setFont (f);
        l->setAlignment (Qt::AlignHCenter);
        l->setFixedWidth (labelWidth);
        return l;
    }

    void addKnob (DeviceTile* tile, int insert, int parameter, const QString& name, int steps)
    {
        const auto n = (int) std::count_if (knobs.begin(), knobs.end(), [insert] (const Knob& k) { return k.insert == insert; });
        auto* column = new QWidget (tile);
        auto* v = new QVBoxLayout (column);
        v->setContentsMargins (0, 0, 0, 0);
        v->setSpacing (0);
        auto* label = smallLabel (QFontMetrics (font()).elidedText (name, Qt::ElideRight, labelWidth - 2), column);
        label->setToolTip (name);
        auto* pot = new Potentiometer (column);
        pot->setFixedSize (knobSize, knobSize);
        pot->setFocusPolicy (Qt::NoFocus);
        pot->setWrapping (false);
        pot->setAccessibleName (name);
        pot->setRange (0, parameter < 0 ? 100 : steps > 1 ? steps - 1 : resolution);
        auto* value = smallLabel ({}, column);
        value->setForegroundRole (QPalette::PlaceholderText);
        v->addWidget (label);
        v->addWidget (pot, 0, Qt::AlignHCenter);
        v->addWidget (value);
        tile->body->addWidget (column, n / knobsPerRow, n % knobsPerRow);

        Knob k { insert, parameter, steps, pot, value };
        const auto index = knobs.size();
        knobs.push_back (k);
        QObject::connect (pot, &QDial::valueChanged, this, [this, index] (int position)
        {
            if (syncing)
                return;
            const auto& kn = knobs[index];
            const auto list = window.engine.inserts (track);
            if (kn.insert >= (int) list.size())
                return;
            if (kn.parameter < 0)
                window.setInsertMix (track, kn.insert, (float) position / 100.0f);
            else
            {
                const auto& fx = list[(size_t) kn.insert].effect;
                fx->setParameter (kn.parameter, (float) position / (float) std::max (1, kn.dial->maximum()));
                window.changed();
            }
        });
        if (parameter < 0)
            pot->setToolTip (MixerWindow::tr ("Dry/Wet: how much of the effect is heard against the sound going in (double-click: all wet)"));
        pot->installEventFilter (this);
        pot->setProperty ("knob", (int) index);
    }

    bool eventFilter (QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::MouseButtonDblClick)
            if (const auto k = o->property ("knob"); k.isValid())
            {
                const auto& kn = knobs[(size_t) k.toInt()];
                if (kn.parameter < 0)
                    kn.dial->setValue (100);
                return true;
            }
        return QWidget::eventFilter (o, e);
    }

    void rebuild (int t)
    {
        clear();
        track = t;
        if (track < 0)
            return;
        const auto colour = window.info[(size_t) track].colour;

        // The instrument, first on the left.
        {
            auto* tile = new DeviceTile (row, colour);
            const auto inst = window.engine.instrument (track);
            const bool audioOnly = window.engine.strip (track).mode.load() == (int) mixer::TrackMode::audio;
            auto* title = new QLabel (inst != nullptr ? QString::fromStdString (inst->name()) : MixerWindow::tr ("No instrument"), tile);
            auto bold = title->font();
            bold.setBold (true);
            title->setFont (bold);
            tile->head->addWidget (title, 1);
            auto* edit = new QToolButton (tile);
            edit->setText (MixerWindow::tr ("Edit"));
            edit->setEnabled (inst != nullptr);
            edit->setToolTip (MixerWindow::tr ("Open the instrument: the Synth's settings, the Sampler or Slicer, or the plugin's window"));
            QObject::connect (edit, &QToolButton::clicked, this, [this] { window.editInstrument (track); });
            tile->head->addWidget (edit);
            auto* choose = new QToolButton (tile);
            choose->setText (QStringLiteral ("▾"));
            choose->setToolTip (MixerWindow::tr ("Choose the instrument"));
            QObject::connect (choose, &QToolButton::clicked, this, [this, choose] { instrumentMenu (choose->mapToGlobal (QPoint (0, choose->height()))); });
            tile->head->addWidget (choose);
            auto* note = new QLabel (inst == nullptr ? MixerWindow::tr ("Drop an instrument here, or on the track's strip, to play its MIDI clips")
                                     : audioOnly ? MixerWindow::tr ("The track plays only its audio: Inst or Hybrid in the Inspector lets this play")
                                     : MixerWindow::tr ("Plays the MIDI clips and the keyboard. Drop another instrument here to replace it"), tile);
            note->setWordWrap (true);
            note->setForegroundRole (QPalette::PlaceholderText);
            note->setFixedWidth (190);
            tile->body->addWidget (note, 0, 0);
            tile->setFixedWidth (220);
            rowLayout->addWidget (tile);
        }

        // The inserts, in the order they play.
        const auto list = window.engine.inserts (track);
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& fx = list[(size_t) i].effect;
            auto* tile = new DeviceTile (row, colour);
            auto* power = new QToolButton (tile);
            power->setCheckable (true);
            power->setText (QStringLiteral ("⏻"));
            power->setToolTip (MixerWindow::tr ("On, or bypassed"));
            QObject::connect (power, &QToolButton::toggled, this, [this, i] (bool on)
            {
                if (! syncing)
                    window.setInsertBypassed (track, i, ! on);
            });
            powers.push_back (power);
            tile->head->addWidget (power);
            auto* title = new QLabel (QString::fromStdString (fx->name()), tile);
            auto bold = title->font();
            bold.setBold (true);
            title->setFont (bold);
            tile->head->addWidget (title, 1);
            auto* edit = new QToolButton (tile);
            edit->setText (MixerWindow::tr ("Edit"));
            edit->setToolTip (MixerWindow::tr ("All of its settings, in its own window"));
            QObject::connect (edit, &QToolButton::clicked, this, [this, i] { window.editInsert (track, i); });
            tile->head->addWidget (edit);
            tile->setContextMenuPolicy (Qt::CustomContextMenu);
            QObject::connect (tile, &QWidget::customContextMenuRequested, this, [this, i, tile] (QPoint at) { window.insertMenu (track, i, tile->mapToGlobal (at)); });

            const auto params = fx->parameters();
            int shown = 0;
            for (int k = 0; k < (int) params.size() && shown < maxKnobs; ++k, ++shown)
                addKnob (tile, i, k, QString::fromStdString (params[(size_t) k].name), params[(size_t) k].toggle ? 2 : params[(size_t) k].steps);
            if (params.empty())
            {
                auto* note = new QLabel (MixerWindow::tr ("Its knobs are in its window: Edit"), tile);
                note->setForegroundRole (QPalette::PlaceholderText);
                tile->body->addWidget (note, 1, 0, 1, knobsPerRow);
            }
            addKnob (tile, i, -1, MixerWindow::tr ("Dry/Wet"), 0);
            rowLayout->addWidget (tile);
        }

        // Then a card that adds an effect.
        auto* adder = new DeviceTile (row, colour, true);
        auto* add = new QPushButton (MixerWindow::tr ("+\nAdd effect"), adder);
        add->setFlat (true);
        add->setEnabled ((int) list.size() < mixer::maxInserts);
        add->setToolTip (MixerWindow::tr ("Add an effect after the others (or drop one here from the browser's Audio Effects)"));
        QObject::connect (add, &QPushButton::clicked, this, [this, add] { window.chooseInsert (track, -1, add->mapToGlobal (QPoint (0, add->height()))); });
        adder->body->addWidget (add, 0, 0);
        adder->setFixedWidth (110);
        rowLayout->addWidget (adder);
        rowLayout->addStretch (1);
    }

    void sync()
    {
        if (track < 0)
            return;
        syncing = true;
        const auto list = window.engine.inserts (track);
        for (size_t i = 0; i < powers.size() && i < list.size(); ++i)
            powers[i]->setChecked (! list[i].bypassed);
        std::vector<std::vector<mixer::Effect::Parameter>> params (list.size());
        for (auto& kn : knobs)
        {
            if (kn.insert >= (int) list.size())
                continue;
            const auto& x = list[(size_t) kn.insert];
            if (kn.parameter < 0)
            {
                if (! kn.dial->isSliderDown())
                    kn.dial->setValue ((int) std::lround (x.mix * 100.0f));
                kn.value->setText (x.mix >= 0.995f ? MixerWindow::tr ("Wet") : x.mix <= 0.005f ? MixerWindow::tr ("Dry")
                                                    : QStringLiteral ("%1%").arg ((int) std::lround (x.mix * 100.0f)));
                continue;
            }
            auto& p = params[(size_t) kn.insert];
            if (p.empty())
                p = x.effect->parameters();
            if (kn.parameter >= (int) p.size())
                continue;
            const auto& par = p[(size_t) kn.parameter];
            if (! kn.dial->isSliderDown())
                kn.dial->setValue ((int) std::lround (par.value * kn.dial->maximum()));
            kn.value->setText (QString::fromStdString (par.text));
        }
        syncing = false;
    }

    void instrumentMenu (QPoint at)
    {
        QMenu menu (this);
        const auto choose = [this] (const QByteArray& what) { return [this, what] { window.dropPlugin (track, what); }; };
        menu.addAction (MixerWindow::tr ("&Synth"), this, choose ("synth"));
        menu.addAction (MixerWindow::tr ("S&ampler"), this, choose ("sampler"));
        menu.addAction (MixerWindow::tr ("S&licer"), this, choose ("slicer"));
        menu.addAction (QString::fromUtf8 ("PHI &rāga"), this, choose ("raga"));
        const auto plugins = window.audio.knownInstruments();
        if (! plugins.empty())
            menu.addSeparator();
        for (const auto& p : plugins)
            menu.addAction (QString::fromStdString (p.name) + QStringLiteral (" (") + QString::fromStdString (p.format) + QLatin1Char (')'),
                            this, choose ("instrument\n" + QByteArray::fromStdString (p.id)));
        if (window.engine.instrument (track) != nullptr)
        {
            menu.addSeparator();
            menu.addAction (MixerWindow::tr ("&No Instrument"), this, [this] { window.setInstrumentKind (track, noInstrument, {}); window.devices->refresh(); });
        }
        menu.exec (at);
    }

    MixerWindow& window;
    QScrollArea* scroll;
    QWidget* row;
    QHBoxLayout* rowLayout;
    QString signature;
    int track = -1;
    std::vector<Knob> knobs;
    std::vector<QToolButton*> powers;
    bool syncing = false;
};

//==============================================================================
// The clip picked in the launcher or the tracks, drawn in the Device Chain's place: an audio
// clip's waveform, or a MIDI clip's notes, not to be edited here (double-click an audio clip of
// the tracks to edit it in the editor).
class ClipView final : public QWidget
{
public:
    explicit ClipView (MixerWindow& w, QWidget* parent = nullptr) : QWidget (parent != nullptr ? parent : &w), window (w)
    {
        setAccessibleName (MixerWindow::tr ("Clip"));
        setMinimumHeight (120);
        // A MIDI clip's Φ right on its notes (owner, 2026-10-25): the same pitches, dealt out
        // again in time, another order each press.
        phi = new PhiButton (MixerWindow::tr ("Φ: the notes' places dealt out again"), this, false);
        phi->setToolTip (MixerWindow::tr ("Φ: the same pitches and rhythm, the notes dealt out again along the golden ratio. Again for another order"));
        phi->setFocusPolicy (Qt::NoFocus);
        phi->hide();
        QObject::connect (phi, &QToolButton::clicked, this, [this] { window.phiShownMidi(); });
    }

    int shownTrack() const { return track; }
    uint32_t shownId() const { return id; }
    int shownScene() const { return scene; }
    bool showsMidi() const { return found && clip.isMidi(); }
    QString shownName() const { return name; }

    // id: the clip on the track's timeline, or 0 with the scene of a launcher slot.
    void set (int t, uint32_t clipId, int s)
    {
        track = t;
        id = clipId;
        scene = s;
        reload();
    }

    // Again from the mix (after an edit); false when the clip is gone.
    bool reload()
    {
        found = false;
        if (track < 0 || track >= window.engine.numTracks())
            return false;
        if (id != 0)
        {
            for (const auto& c : window.engine.clips (track))
                if (c.id == id)
                {
                    clip = c;
                    found = true;
                    const auto path = c.audio != nullptr ? c.audio->path : c.midi != nullptr ? c.midi->path : std::string();
                    name = QFileInfo (QString::fromStdString (path)).completeBaseName();
                }
        }
        else if (scene >= 0 && scene < mixer::maxScenes)
        {
            if (const auto slot = window.slots[(size_t) track][(size_t) scene]; slot != nullptr)
            {
                clip = slot->clip;
                found = true;
                name = window.slotName (track, scene);
            }
        }
        // A hybrid track's clip with the clip of the other kind it plays with: the one on the
        // track that overlaps it most, drawn beside it (owner, 2026-10-25: "1/2 audio and 1/2 MIDI").
        hasPartner = false;
        if (found && id != 0 && window.engine.strip (track).mode.load() == (int) mixer::TrackMode::hybrid)
        {
            Frame most = 0;
            for (const auto& c : window.engine.clips (track))
            {
                if (c.isMidi() == clip.isMidi() || c.muted)
                    continue;
                const auto overlap = std::min (c.end(), clip.end()) - std::max (c.start, clip.start);
                if (overlap > most)
                {
                    most = overlap;
                    partner = c;
                    hasPartner = true;
                }
            }
        }
        measure();
        phi->setVisible (found && clip.isMidi());
        placePhi();
        update();
        return found;
    }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::Antialiasing);
        const auto colour = track >= 0 ? window.info[(size_t) track].colour : palette().color (QPalette::Highlight);
        const auto r = QRectF (rect()).adjusted (6.5, 4.5, -6.5, -4.5);
        g.setPen (QPen (palette().color (QPalette::Mid), 1.0));
        g.setBrush (palette().color (QPalette::Base));
        g.drawRoundedRect (r, 6, 6);
        if (! found)
            return;

        // Its name in white on the track's colour, then what it is.
        const QRectF nameBox (r.left() + 8, r.top() + 6, std::min (260.0, r.width() - 16), 20);
        g.setPen (Qt::NoPen);
        g.setBrush (colour);
        g.drawRoundedRect (nameBox, 3, 3);
        g.setPen (Qt::white);
        g.drawText (nameBox.adjusted (6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    g.fontMetrics().elidedText (name, Qt::ElideRight, (int) nameBox.width() - 12));
        const auto seconds = (double) clip.length / std::max (1.0, window.rate());
        auto about = (clip.audio != nullptr && clip.midi != nullptr ? MixerWindow::tr ("MIDI over audio")
                      : clip.isMidi()                              ? MixerWindow::tr ("MIDI")
                                                                   : MixerWindow::tr ("Audio")) + QStringLiteral ("  ·  ")
                     + QStringLiteral ("%1:%2").arg ((int) seconds / 60).arg (std::fmod (seconds, 60.0), 5, 'f', 2, QLatin1Char ('0'));
        if (clip.reversed)
            about += QStringLiteral ("  ·  ") + MixerWindow::tr ("reversed");
        if (clip.muted)
            about += QStringLiteral ("  ·  ") + MixerWindow::tr ("muted");
        if (id != 0 && ! clip.isMidi())
            about += QStringLiteral ("  ·  ") + MixerWindow::tr ("double-click to edit it in the editor");
        g.setPen (palette().color (QPalette::PlaceholderText));
        g.drawText (QRectF (nameBox.right() + 10, nameBox.top(), r.right() - nameBox.right() - 18, nameBox.height()), Qt::AlignVCenter | Qt::AlignLeft, about);

        const auto area = QRectF (r.left() + 8, nameBox.bottom() + 8, r.width() - 16, r.bottom() - nameBox.bottom() - 14);
        if (area.height() < 10 || area.width() < 10)
            return;
        auto waveArea = area, notesArea = area;
        if (clip.audio != nullptr && clip.midi != nullptr)
        {
            // A slot's two layers, as in the grid: the MIDI on top, the audio below, each named.
            const auto half = (area.height() - 8) / 2.0;
            notesArea.setHeight (half);
            waveArea.setTop (area.bottom() - half);
            g.setPen (QPen (palette().color (QPalette::Mid), 1.0, Qt::DotLine));
            g.drawLine (QPointF (area.left(), area.center().y()), QPointF (area.right(), area.center().y()));
            g.setPen (palette().color (QPalette::PlaceholderText));
            const auto file = [] (const std::string& path) { return QFileInfo (QString::fromStdString (path)).completeBaseName(); };
            g.drawText (QRectF (notesArea.left(), notesArea.top(), notesArea.width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
                        MixerWindow::tr ("MIDI: %1").arg (file (clip.midi->path)));
            g.drawText (QRectF (waveArea.left(), waveArea.top(), waveArea.width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
                        MixerWindow::tr ("Audio: %1").arg (file (clip.audio->path)));
            notesArea.setTop (notesArea.top() + 16);
            waveArea.setTop (waveArea.top() + 16);
        }
        else if (hasPartner)
        {
            // Half each: the audio on the left, the MIDI on the right, each named.
            const auto half = (area.width() - 12) / 2.0;
            waveArea.setWidth (half);
            notesArea.setLeft (area.right() - half);
            g.setPen (QPen (palette().color (QPalette::Mid), 1.0));
            g.drawLine (QPointF (area.center().x(), area.top()), QPointF (area.center().x(), area.bottom()));
            const auto& partnerPath = partner.audio != nullptr ? partner.audio->path : partner.midi->path;
            const auto other = QFileInfo (QString::fromStdString (partnerPath)).completeBaseName();
            g.setPen (palette().color (QPalette::PlaceholderText));
            const auto label = [&] (const QRectF& at, const QString& kind, const QString& who)
            {
                g.drawText (QRectF (at.left(), at.top(), at.width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
                            g.fontMetrics().elidedText (kind + QStringLiteral (": ") + who, Qt::ElideRight, (int) at.width()));
            };
            label (waveArea, MixerWindow::tr ("Audio"), clip.isMidi() ? other : name);
            label (notesArea, MixerWindow::tr ("MIDI"), clip.isMidi() ? name : other);
            waveArea.setTop (waveArea.top() + 16);
            notesArea.setTop (notesArea.top() + 16);
        }
        if (! notes.empty() || notePart.midi != nullptr)
            paintNotes (g, notesArea, colour);
        if (wavePart.audio != nullptr)
        {
            g.setPen (QPen (palette().color (QPalette::Mid), 1.0));
            g.drawLine (QPointF (waveArea.left(), waveArea.center().y()), QPointF (waveArea.right(), waveArea.center().y()));
            paintWave (g, waveArea, colour);
        }
    }

    void resizeEvent (QResizeEvent*) override { placePhi(); }

    void mouseDoubleClickEvent (QMouseEvent*) override
    {
        if (found && id != 0 && ! clip.isMidi())
            Q_EMIT window.editClipRequested (track, id);
    }

private:
    struct Note { Frame start, end; int pitch, velocity; };

    // The heard part, in columns of peaks, or its notes.
    void measure()
    {
        peaks.clear();
        notes.clear();
        wavePart = notePart = {};
        if (! found || clip.length <= 0)
            return;
        if (clip.audio != nullptr)
            wavePart = clip;
        if (clip.midi != nullptr)
            notePart = clip;   // a slot with both layers fills both
        if (hasPartner)
            (partner.isMidi() ? notePart : wavePart) = partner;
        if (wavePart.audio != nullptr && wavePart.length > 0)
        {
            const auto& clip = wavePart;
            const auto& a = *clip.audio;
            const auto total = a.length();
            constexpr int columns = 2048;
            peaks.assign (columns, { 0.0f, 0.0f });
            for (int c = 0; c < columns; ++c)
            {
                const auto from = clip.offset + clip.length * c / columns;
                const auto to = std::max (from + 1, clip.offset + clip.length * (c + 1) / columns);
                float lo = 0.0f, hi = 0.0f;
                for (Frame f = from; f < to; f += std::max<Frame> (1, (to - from) / 64))
                {
                    const auto at = clip.reversed ? total - 1 - f : f;
                    if (at < 0 || at >= total)
                        continue;
                    const auto v = 0.5f * (a.left[(size_t) at] + a.right[(size_t) at]);
                    lo = std::min (lo, v);
                    hi = std::max (hi, v);
                }
                peaks[(size_t) c] = { lo, hi };
            }
        }
        if (notePart.midi != nullptr)
        {
            const auto& clip = notePart;
            std::map<int, std::pair<Frame, int>> held;   // pitch and channel: started at, velocity
            for (const auto& e : clip.midi->events)
            {
                const auto kind = e.status & 0xF0;
                const auto key = (e.status & 0x0F) * 128 + e.data1;
                if (kind == 0x90 && e.data2 > 0)
                    held[key] = { e.frame, e.data2 };
                else if (kind == 0x80 || (kind == 0x90 && e.data2 == 0))
                    if (auto it = held.find (key); it != held.end())
                    {
                        notes.push_back ({ it->second.first, e.frame, e.data1, it->second.second });
                        held.erase (it);
                    }
            }
            for (const auto& [key, start] : held)
                notes.push_back ({ start.first, clip.midi->length, key % 128, start.second });
            const auto from = clip.offset, to = clip.offset + clip.length;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.end <= from || n.start >= to; }), notes.end());
        }
    }

    void paintWave (QPainter& g, const QRectF& area, const QColor& colour)
    {
        if (peaks.empty())
            return;
        const auto mid = area.center().y(), half = area.height() / 2.0;
        QPainterPath path;
        const auto n = (int) peaks.size();
        const auto columns = std::max (1, (int) area.width());
        for (int x = 0; x < columns; ++x)
        {
            const auto from = x * n / columns, to = std::max (from + 1, (x + 1) * n / columns);
            float lo = 0.0f, hi = 0.0f;
            for (int k = from; k < to && k < n; ++k)
            {
                lo = std::min (lo, peaks[(size_t) k].first);
                hi = std::max (hi, peaks[(size_t) k].second);
            }
            path.addRect (QRectF (area.left() + x, mid - hi * half, 1.0, std::max (1.0, (double) (hi - lo) * half)));
        }
        g.setPen (Qt::NoPen);
        g.setBrush (colour);
        g.drawPath (path);
    }

    void paintNotes (QPainter& g, const QRectF& area, const QColor& colour)
    {
        if (notes.empty())
        {
            g.setPen (palette().color (QPalette::PlaceholderText));
            g.drawText (area, Qt::AlignCenter, MixerWindow::tr ("No notes in the part heard"));
            return;
        }
        int low = 127, high = 0;
        for (const auto& n : notes)
        {
            low = std::min (low, n.pitch);
            high = std::max (high, n.pitch);
        }
        low = std::max (0, low - 2);
        high = std::min (127, high + 2);
        const auto rows = high - low + 1;
        const auto rowHeight = area.height() / rows;
        // The black keys' rows a shade darker, as on a piano roll.
        g.setPen (Qt::NoPen);
        auto shade = palette().color (QPalette::Window);
        shade.setAlphaF (0.6);
        for (int p = low; p <= high; ++p)
            if (const auto k = p % 12; k == 1 || k == 3 || k == 6 || k == 8 || k == 10)
                g.fillRect (QRectF (area.left(), area.bottom() - (p - low + 1) * rowHeight, area.width(), rowHeight), shade);
        const auto from = (double) notePart.offset, span = std::max (1.0, (double) notePart.length);
        for (const auto& n : notes)
        {
            const auto x0 = area.left() + std::max (0.0, (n.start - from) / span) * area.width();
            const auto x1 = area.left() + std::min (1.0, (n.end - from) / span) * area.width();
            auto c = colour;
            c.setAlphaF (0.45 + 0.55 * n.velocity / 127.0);
            g.setBrush (c);
            g.drawRoundedRect (QRectF (x0, area.bottom() - (n.pitch - low + 1) * rowHeight + 0.5, std::max (2.0, x1 - x0 - 1.0), std::max (1.5, rowHeight - 1.0)), 1.5, 1.5);
        }
    }

    void placePhi() { phi->move (width() - phi->width() - 16, 9); }

    MixerWindow& window;
    PhiButton* phi = nullptr;
    int track = -1;
    uint32_t id = 0;
    int scene = -1;
    bool found = false;
    mixer::Clip clip;
    mixer::Clip partner;           // a hybrid track's clip of the other kind, beside it
    bool hasPartner = false;
    mixer::Clip wavePart, notePart;   // what is drawn: the audio and the MIDI
    QString name;
    std::vector<std::pair<float, float>> peaks;
    std::vector<Note> notes;
};

//==============================================================================
// The editor's track (owner, 2026-10-26: "the main editor view is for a single instance, a
// single track in horizontal view, not just for audio editing"): one track of the mix under the
// editor's waveform, Audio only, MIDI only or Audio + MIDI, with the mixer's clip recording and
// editing. Along the top the track, its kind, its instrument, arm, record and play; then its
// launcher slots left to right; then its clips across time; then the clip picked, with the MIDI
// tools that make new MIDI (Φ, Duplicate, ½, ×2, Reverse, a mode or raga, Counterpoint: no note
// editing) or the audio's (Paulstretch, Drums to MIDI). A clip goes from the launcher to the
// timeline and back. A track of two kinds is not saved from here: Render to Editor makes it one
// sound in a tab of its own, for the mastering chain.
class EditorTrack final : public QFrame
{
public:
    EditorTrack (MixerWindow& w, QWidget* parent) : QFrame (parent), window (w)
    {
        setObjectName (QStringLiteral ("editorTrack"));
        setFrameShape (QFrame::NoFrame);
        setAccessibleName (MixerWindow::tr ("The editor's track"));
        setFocusPolicy (Qt::ClickFocus);
        auto* layout = new QVBoxLayout (this);
        layout->setContentsMargins (4, 4, 4, 2);
        layout->setSpacing (3);

        // The bar.
        auto* bar = new QHBoxLayout;
        bar->setSpacing (4);
        trackBox = new QComboBox (this);
        trackBox->setFocusPolicy (Qt::NoFocus);
        trackBox->setToolTip (MixerWindow::tr ("The track of the project the editor shows (the mixer shows them all)"));
        trackBox->setMinimumContentsLength (12);
        QObject::connect (trackBox, &QComboBox::activated, this, [this] (int index) { window.select (index); refresh(); });
        bar->addWidget (trackBox);

        const char* kindNames[] = { QT_TRANSLATE_NOOP ("MixerWindow", "Audio + MIDI"), QT_TRANSLATE_NOOP ("MixerWindow", "Audio"),
                                    QT_TRANSLATE_NOOP ("MixerWindow", "MIDI") };
        const char* kindTips[] = {
            QT_TRANSLATE_NOOP ("MixerWindow", "Audio and MIDI: the audio clips and the instrument together, each layer with its own level; a clip can hold both"),
            QT_TRANSLATE_NOOP ("MixerWindow", "Audio only: the track's audio clips play, and Record records the audio input"),
            QT_TRANSLATE_NOOP ("MixerWindow", "MIDI only: the instrument plays the MIDI clips and the keyboard, and Record records MIDI"),
        };
        for (int m : { 1, 2, 0 })
        {
            auto* b = new QToolButton (this);
            b->setText (MixerWindow::tr (kindNames[m]));
            b->setToolTip (MixerWindow::tr (kindTips[m]));
            b->setCheckable (true);
            b->setAutoRaise (true);
            b->setFocusPolicy (Qt::NoFocus);
            kindButtons[(size_t) m] = b;
            bar->addWidget (b);
            QObject::connect (b, &QToolButton::clicked, this, [this, m] { setKind (m); });
        }
        bar->addSpacing (6);
        instrumentButton = new QPushButton (this);
        instrumentButton->setFocusPolicy (Qt::NoFocus);
        instrumentButton->setToolTip (MixerWindow::tr ("What the MIDI clips and the keyboard play: the Synth, a Sampler, a Slicer, PHI rāga or a plugin"));
        QObject::connect (instrumentButton, &QPushButton::clicked, this, [this] { instrumentMenu(); });
        bar->addWidget (instrumentButton);
        editButton = new QPushButton (MixerWindow::tr ("Edit"), this);
        editButton->setFocusPolicy (Qt::NoFocus);
        editButton->setToolTip (MixerWindow::tr ("The instrument's window"));
        QObject::connect (editButton, &QPushButton::clicked, this, [this] { window.editInstrument (track()); });
        bar->addWidget (editButton);
        bar->addSpacing (6);

        armButton = new QToolButton (this);
        armButton->setText (MixerWindow::tr ("● Arm"));
        armButton->setCheckable (true);
        armButton->setAutoRaise (true);
        armButton->setFocusPolicy (Qt::NoFocus);
        armButton->setToolTip (MixerWindow::tr ("The track gets the MIDI keyboard and the audio input, and records"));
        QObject::connect (armButton, &QToolButton::clicked, this, [this] { window.arm (track()); Q_EMIT window.activated(); refresh(); });
        bar->addWidget (armButton);
        recordButton = new TransportButton (TransportButton::Symbol::record, MixerWindow::tr ("Record on the track"), this);
        recordButton->setFocusPolicy (Qt::NoFocus);
        QObject::connect (recordButton, &QToolButton::clicked, this, [this] { record(); });
        bar->addWidget (recordButton);
        intoBox = new QComboBox (this);
        intoBox->setFocusPolicy (Qt::NoFocus);
        intoBox->addItem (MixerWindow::tr ("into the Launcher"));
        intoBox->addItem (MixerWindow::tr ("onto the Timeline"));
        intoBox->setToolTip (MixerWindow::tr ("Where Record puts the take: the next empty slot (with the mixer's length, quantise and count-in), "
                                              "or the timeline from the playhead"));
        intoBox->setCurrentIndex (appSettings().value (QStringLiteral ("editorTrack/recordInto"), 0).toInt() == 1 ? 1 : 0);
        QObject::connect (intoBox, &QComboBox::activated, this, [] (int index) { appSettings().setValue (QStringLiteral ("editorTrack/recordInto"), index); });
        bar->addWidget (intoBox);
        playButton = new TransportButton (TransportButton::Symbol::play, MixerWindow::tr ("Play or stop the mix (Shift+F3)"), this);
        playButton->setFocusPolicy (Qt::NoFocus);
        QObject::connect (playButton, &QToolButton::clicked, this, [this] { Q_EMIT window.activated(); window.togglePlay(); });
        bar->addWidget (playButton);
        auto* stop = new QToolButton (this);
        stop->setText (MixerWindow::tr ("■ Track"));
        stop->setAutoRaise (true);
        stop->setFocusPolicy (Qt::NoFocus);
        stop->setToolTip (MixerWindow::tr ("Stops the track's launcher clip; ⟲ plays its timeline again"));
        QObject::connect (stop, &QToolButton::clicked, this, [this] { window.engine.stopTrack (track()); });
        bar->addWidget (stop);
        auto* back = new QToolButton (this);
        back->setText (QStringLiteral ("⟲"));
        back->setAutoRaise (true);
        back->setFocusPolicy (Qt::NoFocus);
        back->setToolTip (MixerWindow::tr ("Back to the timeline: the track plays its clips across time again"));
        QObject::connect (back, &QToolButton::clicked, this, [this] { window.engine.backToTimeline (track()); });
        bar->addWidget (back);
        bar->addStretch (1);
        launcherButton = new QToolButton (this);
        launcherButton->setText (MixerWindow::tr ("Launcher"));
        launcherButton->setCheckable (true);
        launcherButton->setAutoRaise (true);
        launcherButton->setFocusPolicy (Qt::NoFocus);
        launcherButton->setChecked (appSettings().value (QStringLiteral ("editorTrack/launcher"), true).toBool());
        launcherButton->setToolTip (MixerWindow::tr ("The track's clip launcher slots, left to right"));
        bar->addWidget (launcherButton);
        auto* render = new QPushButton (MixerWindow::tr ("Render to Editor"), this);
        render->setFocusPolicy (Qt::NoFocus);
        render->setToolTip (MixerWindow::tr ("The track as it sounds (its MIDI through its instrument, and its audio) into a new tab of the editor, "
                                             "one sound for the mastering chain"));
        QObject::connect (render, &QPushButton::clicked, this, [this] { Q_EMIT window.trackToEditorRequested (track()); });
        bar->addWidget (render);
        layout->addLayout (bar);

        // The launcher's row, the timeline and the clip, one above the other.
        auto* split = new QSplitter (Qt::Vertical, this);
        split->setChildrenCollapsible (false);
        gridScroll = new QScrollArea (split);
        gridScroll->setFrameShape (QFrame::NoFrame);
        gridScroll->setWidgetResizable (true);
        gridScroll->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
        grid = new ClipGrid (window, true, gridScroll);
        gridScroll->setWidget (grid);
        gridScroll->setMinimumHeight (64);
        gridScroll->setVisible (launcherButton->isChecked());
        QObject::connect (launcherButton, &QToolButton::toggled, this, [this] (bool on)
        {
            gridScroll->setVisible (on);
            appSettings().setValue (QStringLiteral ("editorTrack/launcher"), on);
        });
        lanes = new TrackLanes (window, true, split);
        lanes->setMinimumHeight (70);

        auto* clipArea = new QWidget (split);
        auto* clipRow = new QHBoxLayout (clipArea);
        clipRow->setContentsMargins (0, 0, 0, 0);
        clipRow->setSpacing (4);
        clip = new ClipView (window, clipArea);
        clip->setMinimumHeight (90);
        clipRow->addWidget (clip, 1);
        tools = new QWidget (clipArea);
        auto* toolGrid = new QGridLayout (tools);
        toolGrid->setContentsMargins (0, 4, 4, 4);
        toolGrid->setSpacing (3);
        clipRow->addWidget (tools);
        buildTools (toolGrid);
        split->addWidget (gridScroll);
        split->addWidget (lanes);
        split->addWidget (clipArea);
        split->setStretchFactor (0, 1);
        split->setStretchFactor (1, 3);
        split->setStretchFactor (2, 2);
        layout->addWidget (split, 1);

        hint = new QLabel (this);
        hint->setForegroundRole (QPalette::PlaceholderText);
        hint->setWordWrap (true);
        layout->addWidget (hint);
        hintTimer.setSingleShot (true);
        QObject::connect (&hintTimer, &QTimer::timeout, this, [this] { hint->setText (MixerWindow::tr (defaultHint)); });
        hint->setText (MixerWindow::tr (defaultHint));

        timer.setInterval (33);
        QObject::connect (&timer, &QTimer::timeout, this, [this] { tick(); });
        refresh();
    }

    void say (const QString& text)
    {
        if (text.isEmpty())
            return;
        hint->setText (text);
        hintTimer.start (6000);
    }

    // The track's name, kind, instrument and buttons as the mix has them.
    void refresh()
    {
        const auto t = track();
        {
            const QSignalBlocker quiet (trackBox);
            trackBox->clear();
            for (int i = 0; i < window.engine.numTracks(); ++i)
            {
                QPixmap swatch (10, 10);
                swatch.fill (window.info[(size_t) i].colour);
                trackBox->addItem (QIcon (swatch), QStringLiteral ("%1  %2").arg (i + 1).arg (window.info[(size_t) i].name));
            }
            trackBox->setCurrentIndex (t);
        }
        const auto mode = window.engine.strip (t).mode.load();
        for (int m = 0; m < 3; ++m)
            kindButtons[(size_t) m]->setChecked (m == mode);
        const auto inst = window.engine.instrument (t);
        instrumentButton->setText (inst != nullptr ? QString::fromStdString (inst->name()) + QStringLiteral (" ▾") : MixerWindow::tr ("Instrument ▾"));
        instrumentButton->setEnabled (mode != (int) mixer::TrackMode::audio);
        editButton->setEnabled (inst != nullptr && mode != (int) mixer::TrackMode::audio);
        armButton->setChecked (window.engine.liveTrack() == t);
        grid->updateMinimum();
        grid->updateGeometry();
        refreshClip();
    }

protected:
    void showEvent (QShowEvent* e) override
    {
        QFrame::showEvent (e);
        refresh();
        timer.start();
    }

    void hideEvent (QHideEvent* e) override
    {
        timer.stop();
        QFrame::hideEvent (e);
    }

    void keyPressEvent (QKeyEvent* e) override
    {
        // The mixer's few keys that make sense for one track.
        const auto mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
        if (mods == Qt::NoModifier && e->key() == Qt::Key_R && window.showingClip && window.clipView->shownTrack() == track())
        {
            window.reverseShownClip();
            return;
        }
        if (mods == Qt::ControlModifier && e->key() == Qt::Key_Z)
        {
            window.undo();
            return;
        }
        if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && e->key() == Qt::Key_Z)
        {
            window.redo();
            return;
        }
        QFrame::keyPressEvent (e);
    }

private:
    static constexpr const char* defaultHint = QT_TRANSLATE_NOOP ("MixerWindow",
        "Drop audio or MIDI files on a slot or the timeline. Arm and Record to play into the track; a slot's dot records into it. "
        "Right-click a clip to send it to the launcher or the timeline.");

    int track() const { return window.editorTrack(); }

    void setKind (int m)
    {
        auto& s = window.engine.strip (track());
        s.mode = m;
        if (m != (int) mixer::TrackMode::audio)
            window.giveSynth (track());   // MIDI needs something to play it
        window.strips[(size_t) track()]->refresh();
        window.rack->refresh();
        window.changed();
        refresh();
    }

    void instrumentMenu()
    {
        QMenu menu (this);
        const auto t = track();
        const auto choose = [this, t] (const QByteArray& what) { return [this, t, what] { window.dropPlugin (t, what); refresh(); }; };
        menu.addAction (MixerWindow::tr ("&Synth"), this, choose ("synth"));
        menu.addAction (MixerWindow::tr ("S&ampler"), this, choose ("sampler"));
        menu.addAction (MixerWindow::tr ("S&licer"), this, choose ("slicer"));
        menu.addAction (QString::fromUtf8 ("PHI &rāga"), this, choose ("raga"));
        const auto plugins = window.audio.knownInstruments();
        if (! plugins.empty())
            menu.addSeparator();
        for (const auto& p : plugins)
            menu.addAction (QString::fromStdString (p.name) + QStringLiteral (" (") + QString::fromStdString (p.format) + QLatin1Char (')'),
                            this, choose ("instrument\n" + QByteArray::fromStdString (p.id)));
        if (window.engine.instrument (t) != nullptr)
        {
            menu.addSeparator();
            menu.addAction (MixerWindow::tr ("&No Instrument"), this, [this, t] { window.setInstrumentKind (t, noInstrument, {}); refresh(); });
        }
        menu.exec (instrumentButton->mapToGlobal (QPoint (0, instrumentButton->height())));
    }

    // Record on this track: armed first, then the mixer's Record, into the launcher or the timeline.
    void record()
    {
        const auto t = track();
        Q_EMIT window.activated();
        if (window.engine.liveTrack() != t && window.engine.recordInfo (t).state == mixer::RecordState::none)
            window.arm (t);
        window.recordPressed (intoBox->currentIndex() == 0 ? 1 : 0);
        refresh();
    }

    QPushButton* tool (const QString& text, const QString& tip, std::function<void()> fn)
    {
        auto* b = new QPushButton (text, tools);
        b->setToolTip (tip);
        b->setFocusPolicy (Qt::NoFocus);
        QObject::connect (b, &QPushButton::clicked, this, [this, fn = std::move (fn)] { fn(); refreshClip(); });
        return b;
    }

    void buildTools (QGridLayout* g)
    {
        title = new QLabel (tools);
        auto bold = title->font();
        bold.setBold (true);
        title->setFont (bold);
        g->addWidget (title, 0, 0, 1, 4);

        // MIDI: new MIDI from the clip's, never its notes edited one by one.
        midiTools = new QWidget (tools);
        auto* m = new QGridLayout (midiTools);
        m->setContentsMargins (0, 0, 0, 0);
        m->setSpacing (3);
        auto* phi = new PhiButton (MixerWindow::tr ("Φ: the notes' places dealt out again"), midiTools, false);
        phi->setToolTip (MixerWindow::tr ("Φ: the same pitches and the same rhythm, the notes dealt out again along the golden ratio. Again for another order"));
        phi->setFocusPolicy (Qt::NoFocus);
        QObject::connect (phi, &QToolButton::clicked, this, [this] { window.phiShownMidi(); refreshClip(); });
        m->addWidget (phi, 0, 0);
        m->addWidget (tool (MixerWindow::tr ("Duplicate"), MixerWindow::tr ("The clip twice as long, the two joined into one, so Φ deals across both"),
                            [this] { window.editShownMidi ([] (const midifile::Song& s) { return midifile::duplicated (s); }, MixerWindow::tr ("Duplicated into one clip")); }),
                      0, 1);
        m->addWidget (tool (QStringLiteral ("½"), MixerWindow::tr ("Half the density: every other note or chord kept. Again for a quarter"),
                            [this] { window.editShownMidi ([] (const midifile::Song& s) { return midifile::halfDensity (s); }, MixerWindow::tr ("Half the density")); }),
                      0, 2);
        m->addWidget (tool (QStringLiteral ("×2"), MixerWindow::tr ("Double the density: each note played twice in its own time. Again for four times"),
                            [this] { window.editShownMidi ([] (const midifile::Song& s) { return midifile::doubleDensity (s); }, MixerWindow::tr ("Double the density")); }),
                      0, 3);
        rootBox = new QComboBox (midiTools);
        rootBox->setFocusPolicy (Qt::NoFocus);
        rootBox->setToolTip (MixerWindow::tr ("The root, or Sa"));
        for (const auto* n : { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" })
            rootBox->addItem (QString::fromLatin1 (n));
        scaleBox = new QComboBox (midiTools);
        scaleBox->setFocusPolicy (Qt::NoFocus);
        scaleBox->setToolTip (MixerWindow::tr ("Every note to the nearest of a mode or raga on the root (drums stay)"));
        scaleBox->addItem (MixerWindow::tr ("Mode or raga..."), -1);
        QString family;
        const auto& scales = midifile::scales();
        for (int i = 0; i < (int) scales.size(); ++i)
        {
            if (family != QLatin1String (scales[(size_t) i].family))
            {
                scaleBox->insertSeparator (scaleBox->count());
                family = QLatin1String (scales[(size_t) i].family);
            }
            scaleBox->addItem (QString::fromUtf8 (scales[(size_t) i].name), i);
        }
        auto applyScale = [this]
        {
            const auto scale = scaleBox->currentData().toInt();
            if (scale < 0)
                return;
            const auto root = rootBox->currentIndex();
            window.editShownMidi ([root, scale] (const midifile::Song& s) { return midifile::toMode (s, root, scale); },
                                  MixerWindow::tr ("In %1 on %2").arg (scaleBox->currentText(), rootBox->currentText()));
            refreshClip();
        };
        QObject::connect (scaleBox, &QComboBox::activated, this, applyScale);
        QObject::connect (rootBox, &QComboBox::activated, this, applyScale);
        m->addWidget (rootBox, 1, 0);
        m->addWidget (scaleBox, 1, 1, 1, 3);
        m->addWidget (tool (MixerWindow::tr ("Reverse"), MixerWindow::tr ("Plays it backwards (R); again turns it back"), [this] { window.reverseShownClip(); }), 2, 0, 1, 2);
        // How many parts, chosen before Counterpoint (owner, 2026-10-26: up to four): the melody
        // and one to three new voices. The Counterpoint window starts from them.
        partsBox = new QComboBox (midiTools);
        partsBox->setFocusPolicy (Qt::NoFocus);
        partsBox->setToolTip (MixerWindow::tr ("The parts Counterpoint writes in: the melody and one, two or three new voices, each on a new track"));
        for (int parts = 2; parts <= 4; ++parts)
            partsBox->addItem (MixerWindow::tr ("%1 parts").arg (parts));
        allBox = new QCheckBox (MixerWindow::tr ("Every voice in the species"), midiTools);
        allBox->setFocusPolicy (Qt::NoFocus);
        allBox->setToolTip (MixerWindow::tr ("Off: as Fux writes in three and four parts, the nearest voice takes the species and the others go note against note"));
        m->addWidget (partsBox, 2, 2, 1, 2);
        m->addWidget (allBox, 3, 0, 1, 2);
        m->addWidget (tool (MixerWindow::tr ("Counterpoint..."), MixerWindow::tr ("New voices against this melody, by Fux's species of counterpoint, in a mode, raga or maqam, each a clip on a new track"),
                            [this]
                            {
                                auto settings = appSettings();
                                settings.setValue (QStringLiteral ("counterpoint/voices"), partsBox->currentIndex() + 1);
                                settings.setValue (QStringLiteral ("counterpoint/all"), allBox->isChecked());
                                window.counterpointShownMidi();
                                loadParts();   // what the window was left with
                            }),
                      3, 2, 1, 2);
        loadParts();
        g->addWidget (midiTools, 1, 0, 1, 4);

        // Audio: the stretch, the drums as MIDI, the editor.
        audioTools = new QWidget (tools);
        auto* a = new QGridLayout (audioTools);
        a->setContentsMargins (0, 0, 0, 0);
        a->setSpacing (3);
        a->addWidget (tool (MixerWindow::tr ("Paulstretch"), MixerWindow::tr ("The clip as it plays, stretched with the editor's Extreme Stretch settings, saved beside the mix"),
                            [this] { window.stretchShownAudio(); }),
                      0, 0);
        a->addWidget (tool (MixerWindow::tr ("Drums to MIDI"), MixerWindow::tr ("Each hit as a note of the drum kit, a MIDI layer over the clip for the track's instrument"),
                            [this] { window.drumsToMidi (window.clipView->shownTrack(), window.clipView->shownId(), window.clipView->shownScene()); }),
                      0, 1);
        a->addWidget (tool (MixerWindow::tr ("Reverse"), MixerWindow::tr ("Plays it backwards (R); again turns it back"), [this] { window.reverseShownClip(); }), 1, 0);
        editAudio = tool (MixerWindow::tr ("Edit in Editor"), MixerWindow::tr ("The clip in a tab of the editor; Ctrl+S or F3 puts the edit back on its clip"),
                          [this] { Q_EMIT window.editClipRequested (window.clipView->shownTrack(), window.clipView->shownId()); });
        a->addWidget (editAudio, 1, 1);
        g->addWidget (audioTools, 2, 0, 1, 4);

        // Between the launcher and the timeline (owner, 2026-10-26).
        sendButton = tool (QString(), QString(), [this]
        {
            if (! window.showingClip)
                return;
            const auto t = window.clipView->shownTrack();
            if (window.clipView->shownId() != 0)
                window.clipToLauncher (t, window.clipView->shownId());
            else
                window.slotToTimeline (t, window.clipView->shownScene());
        });
        g->addWidget (sendButton, 3, 0, 1, 4);
        g->setRowStretch (4, 1);
    }

    void loadParts()
    {
        const auto settings = appSettings();
        partsBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("counterpoint/voices"), 1).toInt(), 1, 3) - 1);
        allBox->setChecked (settings.value (QStringLiteral ("counterpoint/all"), false).toBool());
    }

    // The clip picked (in the editor's track or the mixer) when it is this track's.
    void refreshClip()
    {
        const bool ours = window.showingClip && window.clipView->shownTrack() == track() && window.clipView->reload();
        const auto key = std::make_tuple (ours ? track() : -1, ours ? window.clipView->shownId() : 0u, ours ? window.clipView->shownScene() : -1);
        if (key != shown || ! ours)
        {
            shown = key;
            clip->set (std::get<0> (key), std::get<1> (key), std::get<2> (key));
        }
        else
            clip->reload();
        const bool midi = ours && window.clipView->showsMidi();
        tools->setVisible (ours);
        midiTools->setVisible (midi);
        audioTools->setVisible (ours && ! midi);
        editAudio->setVisible (ours && window.clipView->shownId() != 0);
        if (ours)
        {
            title->setText ((midi ? MixerWindow::tr ("MIDI Clip: %1") : MixerWindow::tr ("Audio Clip: %1")).arg (window.clipView->shownName()));
            const bool onTimeline = window.clipView->shownId() != 0;
            sendButton->setText (onTimeline ? MixerWindow::tr ("Send to Clip Launcher") : MixerWindow::tr ("Send to Timeline"));
            sendButton->setToolTip (onTimeline ? MixerWindow::tr ("A copy of the clip into the track's first empty slot")
                                               : MixerWindow::tr ("A copy of the slot's clip onto the timeline at the playhead (both layers of a mixed clip)"));
        }
    }

    void tick()
    {
        const auto t = track();
        if (trackBox->currentIndex() != t || trackBox->count() != window.engine.numTracks())
            refresh();
        armButton->setChecked (window.engine.liveTrack() == t);
        const auto take = window.engine.recordInfo (t);
        recordButton->setChecked (take.state != mixer::RecordState::none);
        static_cast<TransportButton*> (playButton)->setSymbol (window.engine.isPlaying() || window.engine.countingIn() ? TransportButton::Symbol::pause
                                                                                                                       : TransportButton::Symbol::play);
        if (gridScroll->isVisible())
            grid->update();
        lanes->follow();
        lanes->update();
        if (++slow % 30 == 0 && ! trackBox->view()->isVisible())
            refresh();   // names and colours changed in the mixer
        else if (slow % 10 == 0)
            refreshClip();
    }

    MixerWindow& window;
    QComboBox* trackBox;
    std::array<QToolButton*, 3> kindButtons {};
    QPushButton* instrumentButton;
    QPushButton* editButton;
    QToolButton* armButton;
    TransportButton* recordButton;
    QComboBox* intoBox;
    TransportButton* playButton;
    QToolButton* launcherButton;
    QScrollArea* gridScroll;
    ClipGrid* grid;
    TrackLanes* lanes;
    ClipView* clip;
    QWidget* tools;
    QLabel* title = nullptr;
    QWidget* midiTools = nullptr;
    QWidget* audioTools = nullptr;
    QPushButton* editAudio = nullptr;
    QPushButton* sendButton = nullptr;
    QComboBox* rootBox = nullptr;
    QComboBox* scaleBox = nullptr;
    QComboBox* partsBox = nullptr;
    QCheckBox* allBox = nullptr;
    QLabel* hint;
    QTimer hintTimer;
    QTimer timer;
    std::tuple<int, uint32_t, int> shown { -1, 0u, -1 };
    int slow = 0;
};

QWidget* MixerWindow::editorTrackView (QWidget* parent)
{
    if (editorView == nullptr)
        editorView = new EditorTrack (*this, parent);
    return editorView;
}

int MixerWindow::editorTrack() const
{
    return std::clamp (shownTrack, 0, std::max (0, engine.numTracks() - 1));
}

void MixerWindow::refreshEditorTrack()
{
    if (editorView != nullptr)
        editorView->refresh();
}

// A launcher slot's clip onto the timeline at the playhead: a mixed clip as its two layers,
// the audio and the MIDI starting together, which a track of both kinds plays as one.
void MixerWindow::slotToTimeline (int track, int scene)
{
    if (track < 0 || track >= engine.numTracks() || scene < 0 || scene >= mixer::maxScenes)
        return;
    const auto slot = slots[(size_t) track][(size_t) scene];
    if (slot == nullptr)
        return;
    remember();
    auto list = engine.clips (track);
    const auto at = engine.position();
    const bool both = slot->clip.audio != nullptr && slot->clip.midi != nullptr;
    for (const bool sound : { true, false })
    {
        auto c = slot->clip;
        if (sound ? c.audio == nullptr : c.midi == nullptr)
            continue;
        if (sound)
            c.midi = nullptr;
        else
            c.audio = nullptr;
        c.start = at;
        c.id = 0;
        c.muted = false;
        list.push_back (c);
    }
    editClips (track, std::move (list));
    if (both)
        giveBothLayers (track);
    changed();
    status->setText (tr ("%1 is on %2's timeline at %3%4").arg (slotName (track, scene), trackName (track), timeText ((double) at / rate()),
                                                                engine.launchInfo (track).timeline ? QString() : tr (" (⟲ plays the timeline again)")));
}

// A timeline clip into the track's first empty slot, a loop of its length.
void MixerWindow::clipToLauncher (int track, uint32_t id)
{
    if (track < 0 || track >= engine.numTracks())
        return;
    mixer::Clip clip;
    for (const auto& c : engine.clips (track))
        if (c.id == id)
            clip = c;
    if (clip.audio == nullptr && clip.midi == nullptr)
        return;
    const auto scene = nextSlotFor (track, 0);
    if (scene < 0)
    {
        status->setText (tr ("%1 has no empty slot left: all %2 scenes have a clip.").arg (trackName (track)).arg (mixer::maxScenes));
        return;
    }
    if (scene >= numScenes)
        setScenes (scene + 1);
    auto slot = std::make_shared<mixer::Slot>();
    clip.start = 0;
    clip.id = 0;
    clip.muted = false;
    slot->clip = clip;
    slots[(size_t) track][(size_t) scene] = slot;
    engine.setSlot (track, scene, slot);
    changed();
    grid->update();
    rack->refresh();
    moveCursor (track, scene);
    status->setText (tr ("%1 is in the launcher on %2, %3").arg (slotName (track, scene), trackName (track), sceneName (scene)));
}

// A sound from the editor onto the editor's track at the playhead, saved as a WAV beside the mix.
bool MixerWindow::sendAudioToTimeline (const AudioData& sound, const QString& name, QString& error)
{
    QString file;
    const auto clip = saveClipEdit (sound, name, file, error);
    if (clip == nullptr)
        return false;
    const auto track = editorTrack();
    remember();
    addClip (track, mixer::makeClip (clip, engine.position()));
    if (engine.strip (track).mode.load() == (int) mixer::TrackMode::instrument)
        giveBothLayers (track);   // a MIDI track with audio put on it plays both
    changed();
    status->setText (tr ("%1 is on %2's timeline at %3 (%4)").arg (name, trackName (track), timeText ((double) engine.position() / rate()), file));
    return true;
}

// An audio file dropped on the editor's track while it is MIDI only (owner, 2026-10-26): its drums
// as MIDI, the track staying MIDI only (the audio silent under the notes), or the audio itself,
// the track then Audio + MIDI. Into `scene`'s slot, or onto the timeline at `at` (scene -1).
// False when it is not that case, and the drop goes on as usual.
bool MixerWindow::audioIntoMidiTrack (int track, const QString& file, int scene, Frame at)
{
    if (track < 0 || track >= engine.numTracks() || isMidiFile (file)
        || engine.strip (track).mode.load() != (int) mixer::TrackMode::instrument)
        return false;
    QMenu menu (editorView != nullptr ? static_cast<QWidget*> (editorView) : this);
    auto* asMidi = menu.addAction (tr ("Drums to &MIDI: the track stays MIDI only"));
    auto* asAudio = menu.addAction (tr ("As &Audio: the track becomes Audio + MIDI"));
    menu.addSeparator();
    menu.addAction (tr ("Cancel"));
    const auto* chosen = menu.exec (QCursor::pos());
    if (chosen == asAudio)
    {
        giveBothLayers (track);
        refreshEditorTrack();
        return false;   // the usual drop, now heard
    }
    if (chosen != asMidi)
        return true;
    if (scene >= 0 && slots[(size_t) track][(size_t) std::clamp (scene, 0, mixer::maxScenes - 1)] != nullptr)
        scene = nextSlotFor (track, scene);
    if (scene >= mixer::maxScenes)
        return true;
    QPointer<MixerWindow> self (this);
    if (scene >= 0)
        loadingSlots.insert ({ track, scene });
    decodeAudio (track, file, [self, track, scene, at] (mixer::AudioClipPtr sound)
    {
        if (self == nullptr)
            return;
        if (scene >= 0)
            self->loadingSlots.erase ({ track, scene });
        if (sound == nullptr)
            return;
        if (scene >= 0)
        {
            if (scene >= self->numScenes)
                self->setScenes (scene + 1);
            auto slot = std::make_shared<mixer::Slot>();
            slot->clip = mixer::makeClip (sound, 0);
            self->slots[(size_t) track][(size_t) scene] = slot;
            self->engine.setSlot (track, scene, slot);
            self->changed();
            self->drumsToMidi (track, 0, scene);
        }
        else
        {
            self->remember();
            self->addClip (track, mixer::makeClip (sound, at));
            self->drumsToMidi (track, self->nextClipId - 1, -1);
        }
        self->engine.strip (track).mode = (int) mixer::TrackMode::instrument;   // drumsToMidi gave it both
        self->strips[(size_t) track]->refresh();
        self->rack->refresh();
        self->refreshEditorTrack();
    });
    return true;
}

MixerWindow::MixerWindow (mixer::Engine& e, AudioEngine& a, Hooks h, QWidget* parent)
    : QWidget (parent, Qt::Window), engine (e), audio (a), hooks (std::move (h))
{
    setWindowTitle (tr ("Mixer"));
    setObjectName (QStringLiteral ("MixerWindow"));
    setFocusPolicy (Qt::StrongFocus);
    restoring = true;   // nothing below is a change to the mix
    Theme::makeTranslucent (this);   // the same theme and window opacity as the rest of PHI
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        std::tie (info[(size_t) i].name, info[(size_t) i].colour) = defaultTrack (i);
        givenDefaults[(size_t) i] = defaultTrack (i);
    }

    // The transport and the mix.
    auto button = [this] (const QString& text, const QString& tip)
    {
        auto* b = new QPushButton (text, this);
        b->setFocusPolicy (Qt::NoFocus);
        b->setToolTip (tip);
        return b;
    };
    tempoBox = new QDoubleSpinBox (this);
    tempoBox->setRange (20.0, 400.0);
    tempoBox->setDecimals (2);
    tempoBox->setValue (120.0);
    tempoBox->setSuffix (tr (" BPM"));
    tempoBox->setToolTip (tr ("The mix's tempo, for the bars and beats shown; the first MIDI file loaded sets it"));
    followTempoBox = new QCheckBox (tr ("MIDI follows tempo"), this);
    followTempoBox->setFocusPolicy (Qt::NoFocus);
    followTempoBox->setToolTip (tr ("Play the MIDI files at the mix's tempo instead of their own tempo maps"));
    launcherButton = button (tr ("Launcher"), tr ("The clip launcher in place of the tracks across time (Performance > Clip Launcher, F5; V goes back)"));
    launcherButton->setCheckable (true);
    inspectorButton = button (tr ("Inspector"), tr ("The Inspector, left of the strips: the selected track's mode, instrument, Pre, inserts, sends and width, and in the launcher its clip"));
    inspectorButton->setCheckable (true);
    inspectorButton->setToolTip (tr ("The Inspector in the Finder's place, left of the tracks: the selected track's mode, instrument, Pre, "
                                     "inserts, sends and width, and the clip picked. Picking a track shows it too; Ctrl+F brings the Finder back"));
    quantiseBox = new QComboBox (this);
    quantiseBox->setFocusPolicy (Qt::NoFocus);
    for (int q = 0; q < numQuantise; ++q)
        quantiseBox->addItem (tr ("Quantise %1").arg (quantiseName (q)));
    quantiseBox->setCurrentIndex (quantiseIndex (4.0));
    quantiseBox->setToolTip (tr ("When a launch starts: at once, or on the next beat division or bar (a clip can have its own)"));
    scenesBox = new QSpinBox (this);
    scenesBox->setRange (1, mixer::maxScenes);
    scenesBox->setValue (numScenes);
    scenesBox->setPrefix (tr ("Scenes "));
    scenesBox->setToolTip (tr ("How many scenes (rows) the launcher has"));
    stripSections = appSettings().value (QStringLiteral ("mixer/stripSections"), allSections).toInt() & allSections;
    wideStrips = appSettings().value (QStringLiteral ("mixer/wideStrips"), false).toBool();
    auto* stripsButton = button (tr ("Strips"), tr ("Which sections the strips and the Inspector show (instrument, Pre, inserts, sends, trim and pan), and wide or narrow strips"));
    qLinkButton = button (tr ("Q-Link"), tr ("Q-Link: a change to one of the picked strips (Ctrl+click picks them) goes to all of them; "
                                             "Alt+Shift while changing one sends it to every track"));
    qLinkButton->setCheckable (true);
    bypassInsertsButton = button (tr ("Bypass Inserts"), tr ("Leave out every track's inserts, to hear the mix without them"));
    bypassInsertsButton->setCheckable (true);
    bypassSendsButton = button (tr ("Bypass Sends"), tr ("Leave out every send, to hear the mix without the returns"));
    bypassSendsButton->setCheckable (true);
    auto* addButton = button (tr ("+ Track"), tr ("Add a track, up to 8 (Ctrl+T)"));
    auto* removeButton = button (tr ("− Track"), tr ("Remove the last track (Ctrl+Shift+T)"));
    auto* openButton = button (tr ("Open..."), tr ("Open a project (Ctrl+O): the mix, the patchbay and the view"));
    auto* saveButton = button (tr ("Save"), tr ("Save the project (Ctrl+S); Ctrl+Shift+S saves it under a new name"));
    auto* mixdownButton = button (tr ("Mix Down to Editor"), tr ("Render the whole mix, with the instruments' tails, into a new tab (Ctrl+Return)"));
    midiButton = button (tr ("MIDI..."), tr ("Choose the MIDI keyboards that play the armed track"));
    midiStatus = new QLabel (this);

    // The transport, the same for the launcher and the tracks across time (owner, 2026-10-25:
    // "a full transport like the audio editor in the mixer view"), takes the tempo and quantise.
    auto* transportRow = buildTransport();
    auto* top = new QHBoxLayout;
    top->addWidget (launcherButton);
    top->addWidget (scenesBox);
    top->addWidget (inspectorButton);
    top->addStretch (1);
    top->addWidget (qLinkButton);
    top->addWidget (bypassInsertsButton);
    top->addWidget (bypassSendsButton);
    top->addWidget (stripsButton);
    top->addWidget (midiButton);
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
    // See-through like the return strips when the window is.
    scroll->viewport()->setAutoFillBackground (false);
    stripArea->setAutoFillBackground (false);

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
    for (int r = 0; r < mixer::numReturns; ++r)
    {
        returnStrips[(size_t) r] = new ReturnStripWidget (*this, r);
        mixRow->addWidget (returnStrips[(size_t) r]);
    }
    mixRow->addWidget (masterStrip);

    // The tracks across time above the strips, as in a DAW's arrangement.
    lanes = new TrackLanes (*this);
    // Or the clip launcher in their place: the grid (scenes on its left), then the rack (the
    // Inspector) on the right when it is asked for (owner, 2026-10-25: "a different place for the inspector").
    rack = new LauncherRack (*this);
    grid = new ClipGrid (*this);
    launcherArea = new QWidget (this);
    auto* launcherRow = new QHBoxLayout (launcherArea);
    launcherRow->setContentsMargins (0, 0, 0, 0);
    // The scenes scroll when the strips need the room (owner, 2026-10-25: more scenes, shorter strips).
    gridScroll = new QScrollArea (launcherArea);
    gridScroll->setWidget (grid);
    gridScroll->setWidgetResizable (true);
    gridScroll->setFrameShape (QFrame::NoFrame);
    gridScroll->setFocusPolicy (Qt::NoFocus);
    gridScroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    gridScroll->viewport()->setAutoFillBackground (false);
    grid->setAutoFillBackground (false);   // setWidget turns it on; the grid fades its own background
    gridScroll->setMinimumHeight (120);
    launcherRow->addWidget (gridScroll, 1);
    launcherArea->hide();
    upperArea = new QWidget (this);
    auto* upperLayout = new QVBoxLayout (upperArea);
    upperLayout->setContentsMargins (0, 0, 0, 0);
    upperLayout->addWidget (lanes);
    upperLayout->addWidget (launcherArea);
    splitter = new QSplitter (Qt::Vertical, this);
    splitter->setChildrenCollapsible (false);
    splitter->addWidget (upperArea);
    splitter->addWidget (mixArea);
    splitter->setStretchFactor (0, 2);
    splitter->setStretchFactor (1, 3);
    splitter->restoreState (appSettings().value (QStringLiteral ("mixer/splitter")).toByteArray());
    lanes->setVisible (appSettings().value (QStringLiteral ("mixer/lanes"), true).toBool());
    updateUpperArea();

    // The file browser on the left of it all.
    browser = new FileBrowser (*this);
    sideSplitter = new QSplitter (Qt::Horizontal, this);
    sideSplitter->setChildrenCollapsible (false);
    // The Inspector (owner, 2026-10-25): a column left of the tracks with the selected track's
    // deep settings (mode, instrument, layers and duck, Pre, every insert, sends, width) and the
    // clip under the launcher's cursor; the strips stay lean.
    inspectorArea = new QWidget (this);
    inspectorLayout = new QVBoxLayout (inspectorArea);
    inspectorLayout->setContentsMargins (0, 0, 0, 0);
    inspectorLayout->setSpacing (4);
    // A MIDI clip picked: its fast edits at the top (owner, 2026-10-25), and the MIDI editor for
    // picking and moving the notes by hand (owner, 2026-10-26); R reverses it.
    midiClipBox = new QFrame (inspectorArea);
    midiClipBox->setObjectName (QStringLiteral ("midiClipBox"));
    midiClipBox->setStyleSheet (QStringLiteral ("QFrame#midiClipBox { border: 1px solid palette(mid); border-radius: 4px; }"));
    {
        auto* grid = new QGridLayout (midiClipBox);
        grid->setContentsMargins (6, 6, 6, 6);
        grid->setSpacing (4);
        midiClipTitle = new QLabel (midiClipBox);
        auto bold = midiClipTitle->font();
        bold.setBold (true);
        midiClipTitle->setFont (bold);
        grid->addWidget (midiClipTitle, 0, 0, 1, 4);
        auto* phi = new PhiButton (tr ("Φ: the notes' places dealt out again"), midiClipBox, false);
        phi->setToolTip (tr ("Φ: the same pitches and the same rhythm, the notes dealt out again along the golden ratio. "
                             "Again for another order"));
        auto* duplicate = new QPushButton (tr ("Duplicate"), midiClipBox);
        duplicate->setToolTip (tr ("The clip twice as long, the two joined into one, so Φ deals across both"));
        auto* half = new QPushButton (QStringLiteral ("½"), midiClipBox);
        half->setToolTip (tr ("Half the density: every other note or chord kept. Again for a quarter"));
        auto* twice = new QPushButton (QStringLiteral ("×2"), midiClipBox);
        twice->setToolTip (tr ("Double the density: each note played twice in its own time. Again for four times"));
        grid->addWidget (phi, 1, 0);
        grid->addWidget (duplicate, 1, 1);
        grid->addWidget (half, 1, 2);
        grid->addWidget (twice, 1, 3);
        rootBox = new QComboBox (midiClipBox);
        rootBox->setToolTip (tr ("The root, or Sa"));
        for (const auto* n : { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" })
            rootBox->addItem (QString::fromLatin1 (n));
        scaleBox = new QComboBox (midiClipBox);
        scaleBox->setToolTip (tr ("Every note to the nearest of a mode or raga on the root (drums stay)"));
        scaleBox->addItem (tr ("Mode or raga..."), -1);
        QString family;
        const auto& scales = midifile::scales();
        for (int i = 0; i < (int) scales.size(); ++i)
        {
            if (family != QLatin1String (scales[(size_t) i].family))
            {
                scaleBox->insertSeparator (scaleBox->count());
                family = QLatin1String (scales[(size_t) i].family);
            }
            scaleBox->addItem (QString::fromUtf8 (scales[(size_t) i].name), i);
        }
        // Hypo (owner, 2026-10-26): a raga's notes folded into the octave from below its Sa, Sa kept.
        hypoBox = new QComboBox (midiClipBox);
        hypoBox->setToolTip (tr ("The range: as played, or a hypo range with the same Sa: every note folded into the octave "
                                 "from the fifth (Pa, a fourth below Sa) or from the fourth (Ma, a fifth below). "
                                 "For the ragas that have that svara"));
        auto refreshHypo = [this]
        {
            const auto scale = scaleBox->currentData().toInt();
            const auto chosen = hypoBox->currentData().toInt();
            hypoBox->clear();
            hypoBox->addItem (tr ("Authentic"), (int) mixer::Hypo::none);
            const auto& list = midifile::scales();
            if (scale >= 0 && mixer::hypoFamily (list[(size_t) scale].family))
                for (auto h : { mixer::Hypo::fifth, mixer::Hypo::fourth })
                    if (mixer::hasHypo (list[(size_t) scale].notes, h))
                        hypoBox->addItem (h == mixer::Hypo::fifth ? tr ("Hypo from the 5th") : tr ("Hypo from the 4th"), (int) h);
            hypoBox->setCurrentIndex (std::max (0, hypoBox->findData (chosen)));
            hypoBox->setEnabled (hypoBox->count() > 1);
        };
        refreshHypo();
        grid->addWidget (new QLabel (tr ("Sa"), midiClipBox), 2, 0);
        grid->addWidget (rootBox, 2, 1);
        grid->addWidget (scaleBox, 2, 2);
        grid->addWidget (hypoBox, 2, 3);
        auto* counterpoint = new QPushButton (tr ("Counterpoint..."), midiClipBox);
        counterpoint->setToolTip (tr ("New voices against this melody, by Fux's species of counterpoint, in a mode, raga or "
                                      "maqam, each a clip on a new track"));
        grid->addWidget (counterpoint, 3, 0, 1, 2);
        auto* fugue = new QPushButton (tr ("Fugue..."), midiClipBox);
        fugue->setToolTip (tr ("A fugue on this melody as its subject: two to four voices entering in turn, a countersubject, "
                               "episodes and a stretto, in any mode, raga or maqam, each voice a clip on a new track"));
        grid->addWidget (fugue, 3, 2, 1, 2);
        connect (fugue, &QPushButton::clicked, this, [this] { fugueShownMidi(); });
        auto* stochastic = new QPushButton (tr ("stochasticize..."), midiClipBox);
        stochastic->setToolTip (tr ("A new line by Xenakis's methods: an st cloud or markov screens, its pitches and rhythm through "
                                    "sieves taken from the mode, a rhythmic mode or this clip"));
        grid->addWidget (stochastic, 4, 0, 1, 2);
        connect (stochastic, &QPushButton::clicked, this, [this] { stochasticizeShownMidi(); });
        auto* cube = new QPushButton (tr ("cubify..."), midiClipBox);
        cube->setToolTip (tr ("This clip arranged by Xenakis's cubes, as in nomos alpha: 24 sections onto its track (right-click a "
                              "scene to cubify the whole scene)"));
        grid->addWidget (cube, 4, 2, 1, 2);
        connect (cube, &QPushButton::clicked, this, [this] { cubifyShownMidi(); });
        auto* notesButton = new QPushButton (tr ("Edit Notes..."), midiClipBox);
        notesButton->setToolTip (tr ("The MIDI editor: the notes on a piano roll in 12-TET, or in a mode, raga, maqam, laras or Scala "
                                     "scale, with step input, quantise, restyle and MPE"));
        grid->addWidget (notesButton, 5, 0, 1, 4);
        auto* hint = new QLabel (tr ("R reverses it"), midiClipBox);
        hint->setForegroundRole (QPalette::PlaceholderText);
        grid->addWidget (hint, 6, 0, 1, 4);
        connect (counterpoint, &QPushButton::clicked, this, [this] { counterpointShownMidi(); });
        connect (notesButton, &QPushButton::clicked, this, [this] { noteEditorShownMidi(); });

        connect (phi, &QToolButton::clicked, this, [this] { phiShownMidi(); });
        connect (duplicate, &QPushButton::clicked, this, [this] { editShownMidi ([] (const midifile::Song& s) { return midifile::duplicated (s); }, tr ("Duplicated into one clip")); });
        connect (half, &QPushButton::clicked, this, [this] { editShownMidi ([] (const midifile::Song& s) { return midifile::halfDensity (s); }, tr ("Half the density")); });
        connect (twice, &QPushButton::clicked, this, [this] { editShownMidi ([] (const midifile::Song& s) { return midifile::doubleDensity (s); }, tr ("Double the density")); });
        auto applyScale = [this]
        {
            const auto scale = scaleBox->currentData().toInt();
            if (scale < 0)
                return;
            const auto root = rootBox->currentIndex();
            const auto below = mixer::hypoBelow ((mixer::Hypo) hypoBox->currentData().toInt());
            editShownMidi ([root, scale, below] (const midifile::Song& s)
                           { return below > 0 ? midifile::inRange (*midifile::toMode (s, root, scale), root, below) : midifile::toMode (s, root, scale); },
                           below > 0 ? tr ("In %1 on %2, %3").arg (scaleBox->currentText(), rootBox->currentText(), hypoBox->currentText().toLower())
                                     : tr ("In %1 on %2").arg (scaleBox->currentText(), rootBox->currentText()));
        };
        connect (scaleBox, &QComboBox::activated, this, [refreshHypo, applyScale] { refreshHypo(); applyScale(); });
        connect (rootBox, &QComboBox::activated, this, applyScale);
        connect (hypoBox, &QComboBox::activated, this, applyScale);
    }
    midiClipBox->hide();
    inspectorLayout->addWidget (midiClipBox);
    // An audio clip picked: Paulstretch at the top, with the stretch the editor last used.
    audioClipBox = new QFrame (inspectorArea);
    audioClipBox->setObjectName (QStringLiteral ("audioClipBox"));
    audioClipBox->setStyleSheet (QStringLiteral ("QFrame#audioClipBox { border: 1px solid palette(mid); border-radius: 4px; }"));
    {
        auto* grid = new QGridLayout (audioClipBox);
        grid->setContentsMargins (6, 6, 6, 6);
        grid->setSpacing (4);
        audioClipTitle = new QLabel (audioClipBox);
        auto bold = audioClipTitle->font();
        bold.setBold (true);
        audioClipTitle->setFont (bold);
        grid->addWidget (audioClipTitle, 0, 0, 1, 2);
        auto* stretch = new QPushButton (tr ("Paulstretch"), audioClipBox);
        stretch->setToolTip (tr ("The clip as it plays, stretched this many times longer with the editor's Extreme Stretch "
                                 "settings, saved beside the mix. Ctrl+Z puts a track's clip back"));
        stretchRatioBox = new QDoubleSpinBox (audioClipBox);
        stretchRatioBox->setRange (1.0, 10000.0);
        stretchRatioBox->setDecimals (2);
        stretchRatioBox->setSuffix (tr (" ×"));
        stretchRatioBox->setValue (extremeRatio());
        stretchRatioBox->setToolTip (tr ("How many times longer; shared with the editor's Extreme Stretch"));
        grid->addWidget (stretch, 1, 0);
        grid->addWidget (stretchRatioBox, 1, 1);
        connect (stretch, &QPushButton::clicked, this, [this] { stretchShownAudio(); });
        auto* drums = new QPushButton (tr ("Drums to MIDI"), audioClipBox);
        drums->setToolTip (tr ("Each hit as a note of the drum kit (kick, snare or hi-hat), as loud as it was played: "
                               "a MIDI layer over the clip for the track's instrument to play"));
        grid->addWidget (drums, 2, 0, 1, 2);
        connect (drums, &QPushButton::clicked, this, [this]
        {
            if (showingClip)
                drumsToMidi (clipView->shownTrack(), clipView->shownId(), clipView->shownScene());
        });
    }
    audioClipBox->hide();
    inspectorLayout->addWidget (audioClipBox);
    inspectorLayout->addWidget (rack);
    inspectorLayout->addStretch (1);
    inspectorScroll = new QScrollArea (this);
    inspectorScroll->setWidget (inspectorArea);
    inspectorScroll->setWidgetResizable (true);
    inspectorScroll->setFrameShape (QFrame::NoFrame);
    inspectorScroll->setFocusPolicy (Qt::NoFocus);
    inspectorScroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    inspectorScroll->viewport()->setAutoFillBackground (false);
    inspectorArea->setAutoFillBackground (false);
    // The Finder and the Inspector share one column, the same width in both views
    // (owner, 2026-10-25: "a relatively static view").
    sideStack = new QStackedWidget (this);
    sideStack->addWidget (browser);
    sideStack->addWidget (inspectorScroll);
    sideStack->setMinimumWidth (270);
    sideStack->setMaximumWidth (320);
    sideSplitter->addWidget (sideStack);
    sideSplitter->addWidget (splitter);
    sideSplitter->setStretchFactor (0, 0);
    sideSplitter->setStretchFactor (1, 1);
    sideSplitter->setSizes ({ 260, 1100 });
    sideSplitter->restoreState (appSettings().value (QStringLiteral ("mixer/sideColumn")).toByteArray());

    // F6 and Tab go round the browser, the tracks and the strips.
    auto* nextSection = new QShortcut (QKeySequence (Qt::Key_F6), this);
    connect (nextSection, &QShortcut::activated, this, [this] { cycleFocus (true); });
    auto* previousSection = new QShortcut (QKeySequence (Qt::SHIFT | Qt::Key_F6), this);
    connect (previousSection, &QShortcut::activated, this, [this] { cycleFocus (false); });

    status = new StatusLine (this);
    status->echo = [this] (const QString& text)
    {
        if (editorView != nullptr)
            editorView->say (text);
        Q_EMIT message (text);
    };
    auto* bottom = new QHBoxLayout;
    bottom->addWidget (status, 1);
    midiStatus->hide();   // its text is the MIDI Inputs button's tooltip

    // The transport row scrolls sideways rather than setting how narrow the mixer can be, so
    // the editor's side panels (Find) can widen while the mixer is in the editor's place.
    auto* topRow = new QWidget (this);
    top->setContentsMargins (0, 0, 0, 0);
    topRow->setLayout (top);
    auto* topScroll = new QScrollArea (this);
    topScroll->setWidget (topRow);
    topScroll->setWidgetResizable (true);
    topScroll->setFrameShape (QFrame::NoFrame);
    topScroll->setFocusPolicy (Qt::NoFocus);
    topScroll->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    topScroll->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    topScroll->viewport()->setAutoFillBackground (false);
    topRow->setAutoFillBackground (false);
    topScroll->setFixedHeight (topRow->sizeHint().height() + topScroll->horizontalScrollBar()->sizeHint().height());
    // Every MIDI input's name on one line would do the same.
    midiStatus->setWordWrap (true);

    // The Device Chain along the bottom, the whole width (owner, 2026-10-25: as in Ableton):
    // the selected track's instrument and inserts, or the clip picked.
    devices = new TrackDevices (*this);
    clipView = new ClipView (*this);
    devicePanel = new QStackedWidget (this);
    devicePanel->addWidget (devices);
    devicePanel->addWidget (clipView);
    devicePanel->setAutoFillBackground (false);
    // Fixed along the bottom (owner, 2026-10-25), see-through like the rest of the mixer.
    devicePanel->setFixedHeight (190);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (transportRow);
    layout->addWidget (topScroll);
    layout->addWidget (sideSplitter, 1);
    layout->addWidget (devicePanel);
    layout->addLayout (bottom);

    connect (loopButton, &QAbstractButton::toggled, this, [this] (bool on)
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
    connect (tempoBox, &QDoubleSpinBox::valueChanged, this, [this] (double bpm) { engine.tempo = bpm; });
    connect (launcherButton, &QPushButton::toggled, this, [this] (bool on) { showLauncher (on); });
    connect (inspectorButton, &QPushButton::toggled, this, [this] (bool on)
    {
        showSideFinder (! on);
        if (on)
            setFocus();
    });
    connect (quantiseBox, &QComboBox::activated, this, [this] (int q)
    {
        engine.launchQuantise = quantiseBeats[q];
        changed();
        setFocus();
    });
    connect (scenesBox, &QSpinBox::valueChanged, this, [this] (int n)
    {
        if (n != numScenes)
        {
            setScenes (n);
            changed();
        }
    });
    connect (scenesBox, &QSpinBox::editingFinished, this, [this] { setFocus(); });
    engine.tempo = tempoBox->value();
    engine.launchQuantise = quantiseBeats[quantiseBox->currentIndex()];
    connect (bypassInsertsButton, &QPushButton::toggled, this, [this] (bool on) { engine.bypassInserts = on; });
    connect (bypassSendsButton, &QPushButton::toggled, this, [this] (bool on) { engine.bypassSends = on; });
    connect (stripsButton, &QPushButton::clicked, this, [this, stripsButton] { stripsMenu (stripsButton->mapToGlobal (QPoint (0, stripsButton->height()))); });
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

    defaultReturns();
    rebuildStrips();
    select (0);
    showLauncher (false);
    restoring = false;
    // MTS-ESP, when it was on last time and libMTS is there.
    if (appSettings().value (QStringLiteral ("mixer/mtsEsp"), false).toBool() && mixer::MtsEsp::installed())
    {
        mtsEsp = std::make_unique<mixer::MtsEsp>();
        if (mtsEsp->start() != mixer::MtsEsp::Start::started)
            mtsEsp.reset();
    }
    resize (1180, 860);
}

MixerWindow::~MixerWindow()
{
    mtsEsp.reset();   // the master let go of
    engine.setInput (nullptr);   // the audio thread lets go of the audio input before it goes
    if (capture != nullptr)
        capture->stop();
}

double MixerWindow::rate() const { return engine.sampleRate(); }

QString MixerWindow::folder() const
{
    return appSettings().value (QStringLiteral ("mixer/folder"), QDir::homePath()).toString();
}

void MixerWindow::setMidiStatus (const QString& text)
{
    midiStatus->setText (text);
    midiButton->setToolTip (tr ("Choose the MIDI keyboards that play the armed track") + QStringLiteral ("\n") + text);
}

void MixerWindow::rebuildStrips()
{
    const auto count = engine.numTracks();
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        strips[(size_t) i]->setVisible (i < count);
        strips[(size_t) i]->refresh();
    }
    for (auto* r : returnStrips)
        r->refresh();
    refreshInspector();
    if (selected < mixer::masterIndex && selected >= count)
        select (count - 1);
    if (grid != nullptr)
        grid->update();
    refreshTransport();
}

void MixerWindow::refreshInspector()
{
    if (inspectorPending || inspectorLayout == nullptr)
        return;
    inspectorPending = true;
    QTimer::singleShot (0, this, [this]
    {
        inspectorPending = false;
        const auto track = selected < mixer::masterIndex ? std::min (selected, engine.numTracks() - 1) : -1;
        if (inspectorStrip != nullptr && inspectorStrip->index != track)
        {
            inspectorStrip->deleteLater();
            inspectorStrip = nullptr;
        }
        if (inspectorStrip == nullptr && track >= 0)
        {
            inspectorStrip = new MixerStrip (*this, track, true);
            inspectorLayout->insertWidget (inspectorLayout->indexOf (audioClipBox) + 1, inspectorStrip);
        }
        if (inspectorStrip != nullptr)
        {
            inspectorStrip->refresh();
            inspectorStrip->applyLayout();
        }
        if (track >= 0)
            strips[(size_t) track]->refresh();
        rack->refresh();
        if (showingClip && ! clipView->reload())
            showDevices();   // the clip went
        else if (! showingClip)
            devices->refresh();
        refreshMidiClipBox();
    });
}

void MixerWindow::phiShownMidi()
{
    const auto turn = phiTurn++;
    editShownMidi ([turn] (const midifile::Song& s) { return midifile::phiShuffle (s, turn); }, tr ("Φ: the notes dealt out again"));
}

void MixerWindow::noteEditorShownMidi()
{
    if (! showingClip || ! clipView->showsMidi())
        return;
    const auto track = clipView->shownTrack();
    const auto id = clipView->shownId();
    const auto scene = clipView->shownScene();
    mixer::Clip clip;
    mixer::SlotPtr slot;
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && (slot = slots[(size_t) track][(size_t) scene]) != nullptr)
        clip = slot->clip;
    if (clip.midi == nullptr || clip.midi->song == nullptr)
        return;
    auto song = clip.midi->song;
    if (song->ticksPerQuarter <= 0)
    {
        status->setText (tr ("This MIDI file counts in SMPTE time, so it cannot be edited here."));
        return;
    }
    if (clip.reversed)
        song = midifile::reversed (*song);   // edited as it is heard
    const auto tuning = slot != nullptr ? mixer::tuningInForce (slot->tuning, sceneTunings[(size_t) scene], info[(size_t) track].tuning)
                                        : info[(size_t) track].tuning;

    MidiEditor::Host host;
    // Auditions and the editor's playback go to the clip's track, and are not taken as step input.
    host.play = [this] (uint8_t s, uint8_t a, uint8_t b) { engine.liveMidi (s, a, b, false); };
    host.system = tuning.system;
    host.root = tuning.root;
    host.bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    host.folder = folder();
    host.title = QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName();
    host.inputSource = inputSource;
    host.loadAudio = [] (const QString& file, QString& why) -> AudioDataPtr
    {
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
        auto loaded = AudioEngine::loadFile (file.toStdString(), markers, tags, error);
        why = QString::fromStdString (error);
        return loaded;
    };
    const auto armed = engine.liveTrack();
    engine.setLiveTrack (track);
    MidiEditor editor (*song, host, this);
    // The MIDI keyboard's notes, for step input (they reach the track as they always do).
    auto* target = &editor;
    engine.setLiveListener ([target] (uint8_t s, uint8_t a, uint8_t b)
    {
        QMetaObject::invokeMethod (target, [target, s, a, b] { target->midiIn (s, a, b); }, Qt::QueuedConnection);
    });
    const auto accepted = editor.exec() == QDialog::Accepted;
    engine.setLiveListener (nullptr);
    engine.setLiveTrack (armed);
    if (! accepted)
        return;
    const auto edited = editor.result();
    editShownMidi ([edited] (const midifile::Song&) { return edited; }, editor.summary());
}

void MixerWindow::counterpointShownMidi()
{
    if (! showingClip || ! clipView->showsMidi())
        return;
    const auto track = clipView->shownTrack();
    const auto id = clipView->shownId();
    const auto scene = clipView->shownScene();
    mixer::Clip clip;
    mixer::SlotPtr slot;
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && (slot = slots[(size_t) track][(size_t) scene]) != nullptr)
        clip = slot->clip;
    if (clip.midi == nullptr || clip.midi->song == nullptr)
        return;
    const auto title = tr ("Counterpoint");
    auto song = clip.midi->song;
    if (song->ticksPerQuarter <= 0)
    {
        status->setText (tr ("This MIDI file counts in SMPTE time, so it cannot be edited here."));
        return;
    }
    const auto free = mixer::maxTracks - engine.numTracks();
    if (free <= 0)
    {
        QMessageBox::information (this, title, tr ("The new voices go on new tracks, and the mixer has 8 tracks already."));
        return;
    }
    if (clip.reversed)
        song = midifile::reversed (*song);   // written against the melody as it is heard
    const auto melody = counterpoint::cantus (*song);
    if (melody.empty())
    {
        status->setText (tr ("The clip has no notes to write against."));
        return;
    }
    const auto tuning = slot != nullptr ? mixer::tuningInForce (slot->tuning, sceneTunings[(size_t) scene], info[(size_t) track].tuning)
                                        : info[(size_t) track].tuning;

    // What to write.
    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* form = new QFormLayout (&dialog);
    auto* speciesBox = new QComboBox (&dialog);
    speciesBox->addItem (tr ("First species: note against note"));
    speciesBox->addItem (tr ("Second species: two notes against one"));
    speciesBox->addItem (tr ("Third species: four notes against one"));
    speciesBox->addItem (tr ("Fourth species: suspensions, tied over the bar"));
    speciesBox->addItem (tr ("Fifth species: florid, all of them mixed"));
    speciesBox->setToolTip (tr ("Each note of the melody is a bar; the species sets the new voice's rhythm in it"));
    auto* voicesBox = new QSpinBox (&dialog);
    voicesBox->setRange (1, std::min (3, free));
    voicesBox->setToolTip (tr ("New voices, each on a new track"));
    auto* placeBox = new QComboBox (&dialog);
    placeBox->addItem (tr ("Above the melody"));
    placeBox->addItem (tr ("Below the melody"));
    placeBox->addItem (tr ("Around it: above, below, then above again"));
    auto* allBox = new QCheckBox (tr ("Every voice in the species"), &dialog);
    allBox->setToolTip (tr ("Off: as Fux writes in three and four parts, the nearest voice takes the species "
                            "and the others go note against note"));
    auto* modeBox = new QComboBox (&dialog);
    modeBox->addItem (tr ("The melody's own (guessed)"), -1);
    {
        std::string family;
        const auto& all = mixer::tuningSystems();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (all[i].family != family)
            {
                family = all[i].family;
                modeBox->insertSeparator (modeBox->count());
            }
            modeBox->addItem (QString::fromStdString (all[i].name), (int) i);
            modeBox->setItemData (modeBox->count() - 1, QString::fromStdString (all[i].family), Qt::ToolTipRole);
        }
    }
    modeBox->setMaxVisibleItems (24);
    modeBox->setToolTip (tr ("The notes the new voices use, and how their intervals sound: a maqam's neutral third is a third"));
    auto* rootChoice = new QComboBox (&dialog);
    for (const auto* name : rootNames)
        rootChoice->addItem (QString::fromUtf8 (name));
    rootChoice->setToolTip (tr ("The tonic: Sa, the final, or the maqam's root"));
    auto* variation = new QSpinBox (&dialog);
    variation->setRange (1, 9999);
    variation->setToolTip (tr ("Another number writes other lines that keep the same rules"));

    auto settings = appSettings();
    speciesBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("counterpoint/species"), 0).toInt(), 0, 4));
    voicesBox->setValue (settings.value (QStringLiteral ("counterpoint/voices"), 1).toInt());
    placeBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("counterpoint/place"), 0).toInt(), 0, 2));
    allBox->setChecked (settings.value (QStringLiteral ("counterpoint/all"), false).toBool());
    variation->setValue (settings.value (QStringLiteral ("counterpoint/variation"), 0).toInt() + 1);
    // The tuning the clip plays in, else the last mode chosen.
    auto system = tuning.system;
    auto root = tuning.root;
    if (system < 0)
    {
        system = mixer::findTuning (settings.value (QStringLiteral ("counterpoint/mode")).toString().toStdString());
        root = settings.value (QStringLiteral ("counterpoint/root"), melody.back().pitch % 12).toInt();
    }
    modeBox->setCurrentIndex (std::max (0, modeBox->findData (system)));
    rootChoice->setCurrentIndex (std::clamp (root, 0, 11));
    // Hypo (owner, 2026-10-26): for a raga, maqam or dastgah with a fifth or fourth, the new
    // voices kept to the octave from below the tonic; the church modes have their own hypo-modes.
    auto* hypoChoice = new QComboBox (&dialog);
    hypoChoice->setToolTip (tr ("The new voices' range: around the melody as usual, or a hypo range with the same tonic, "
                                "the octave from the fifth (a fourth below the tonic) or from the fourth (a fifth below). "
                                "For ragas, maqams and dastgahs with that degree; the church modes have their hypo-modes in the list"));
    auto lastHypo = settings.value (QStringLiteral ("counterpoint/hypo"), 0).toInt();
    auto rootRow = [&]
    {
        const auto system = modeBox->currentData().toInt();
        rootChoice->setEnabled (system >= 0);
        if (hypoChoice->count() > 0)
            lastHypo = hypoChoice->currentData().toInt();
        hypoChoice->clear();
        hypoChoice->addItem (tr ("Authentic range"), (int) mixer::Hypo::none);
        for (auto h : { mixer::Hypo::fifth, mixer::Hypo::fourth })
            if (system >= 0 && mixer::hasHypo (mixer::tuningSystems()[(size_t) system], h))
                hypoChoice->addItem (h == mixer::Hypo::fifth ? tr ("Hypo, from the fifth") : tr ("Hypo, from the fourth"), (int) h);
        hypoChoice->setCurrentIndex (std::max (0, hypoChoice->findData (lastHypo)));
        hypoChoice->setEnabled (hypoChoice->count() > 1);
    };
    connect (modeBox, &QComboBox::currentIndexChanged, &dialog, rootRow);
    rootRow();

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget (modeBox, 1);
    modeRow->addWidget (rootChoice);
    form->addRow (tr ("Species"), speciesBox);
    form->addRow (tr ("Voices"), voicesBox);
    form->addRow (tr ("Where"), placeBox);
    form->addRow (QString(), allBox);
    form->addRow (tr ("Mode"), modeRow);
    form->addRow (tr ("Range"), hypoChoice);
    form->addRow (tr ("Variation"), variation);
    auto* note = new QLabel (tr ("The melody is %1 notes: each new voice goes on a new track, a clip beside this one.").arg (melody.size()), &dialog);
    note->setWordWrap (true);
    note->setForegroundRole (QPalette::PlaceholderText);
    form->addRow (note);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Write"));
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    counterpoint::Settings options;
    options.species = (counterpoint::Species) (speciesBox->currentIndex() + 1);
    options.voices = voicesBox->value();
    options.placement = (counterpoint::Placement) placeBox->currentIndex();
    options.allInSpecies = allBox->isChecked();
    options.seed = (uint32_t) variation->value();
    const auto chosen = modeBox->currentData().toInt();
    const auto hypo = (mixer::Hypo) hypoChoice->currentData().toInt();
    if (chosen >= 0)
    {
        options.mode = counterpoint::Mode::fromTuning (mixer::tuningSystems()[(size_t) chosen], rootChoice->currentIndex());
        options.mode.below = mixer::hypoBelow (hypo);
    }
    settings.setValue (QStringLiteral ("counterpoint/species"), speciesBox->currentIndex());
    settings.setValue (QStringLiteral ("counterpoint/hypo"), (int) hypo);
    settings.setValue (QStringLiteral ("counterpoint/voices"), options.voices);
    settings.setValue (QStringLiteral ("counterpoint/place"), placeBox->currentIndex());
    settings.setValue (QStringLiteral ("counterpoint/all"), options.allInSpecies);
    settings.setValue (QStringLiteral ("counterpoint/variation"), variation->value());
    settings.setValue (QStringLiteral ("counterpoint/mode"), chosen >= 0 ? QString::fromStdString (mixer::tuningSystems()[(size_t) chosen].plain) : QString());
    settings.setValue (QStringLiteral ("counterpoint/root"), rootChoice->currentIndex());

    writeCounterpoint (track, clip, slot, scene, options, chosen >= 0 ? mixer::TuningChoice { chosen, rootChoice->currentIndex() } : mixer::TuningChoice {},
                       tuning, -1);
}

void MixerWindow::fugueShownMidi()
{
    if (! showingClip || ! clipView->showsMidi())
        return;
    const auto track = clipView->shownTrack();
    const auto id = clipView->shownId();
    const auto scene = clipView->shownScene();
    mixer::Clip clip;
    mixer::SlotPtr slot;
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && (slot = slots[(size_t) track][(size_t) scene]) != nullptr)
        clip = slot->clip;
    if (clip.midi == nullptr || clip.midi->song == nullptr || ! writeFugue (track, clip, slot, scene))
        return;
    rebuildStrips();
    grid->update();
    rack->refresh();
    changed();
}

void MixerWindow::writeCounterpoint (int track, const mixer::Clip& clip, mixer::SlotPtr slot, int scene, const counterpoint::Settings& options,
                                     mixer::TuningChoice modeChoice, mixer::TuningChoice tuning, Frame launchAt)
{
    const auto title = tr ("Counterpoint");
    auto song = clip.midi->song;
    if (clip.reversed)
        song = midifile::reversed (*song);   // written against the melody as it is heard
    const auto melody = counterpoint::cantus (*song);
    if (melody.empty())
        return;
    const bool live = launchAt >= 0;

    // Written on a worker: a long melody in the florid species takes a few seconds.
    QProgressDialog progress (tr ("Writing counterpoint against %1...").arg (QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName()),
                              QString(), 0, 0, this);
    progress.setWindowTitle (title);
    progress.setWindowModality (Qt::WindowModal);
    progress.setMinimumDuration (live ? 2000 : 300);
    QFutureWatcher<counterpoint::Result> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<counterpoint::Result>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture (QtConcurrent::run ([melody, options] { return counterpoint::generate (melody, options); }));
    loop.exec();
    progress.reset();
    const auto result = watcher.result();

    // Each voice a MIDI file beside the mix, then a clip on a new track where this one is. A
    // loop's voices go on the tracks the last loop's went on, when they are still there.
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName();
    if (stem.isEmpty())
        stem = tr ("MIDI");
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    int channel = 0;
    for (int c = 0; c < 16; ++c)
        if (song->channelsUsed & (1u << c))
        {
            channel = c;
            break;
        }
    if (! live)
        remember();
    int made = 0;
    for (size_t v = 0; v < result.voices.size(); ++v)
    {
        if (result.voices[v].empty())
            continue;
        const auto voiceName = QString::fromStdString (result.names[v]);
        const auto trackName = tr ("%1 %2").arg (info[(size_t) track].name, voiceName);
        int target = -1;
        if (live)
            for (int t = 0; t < engine.numTracks(); ++t)
                if (t != track && info[(size_t) t].name == trackName)
                    target = t;
        if (target < 0 && engine.numTracks() >= mixer::maxTracks)
            continue;
        const auto written = counterpoint::toSong (*song, result.voices[v], channel);
        QString file;
        for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
            file = QDir (base).filePath (n == 1 ? QStringLiteral ("%1 counterpoint %2.mid").arg (stem, voiceName)
                                                : QStringLiteral ("%1 counterpoint %2 %3.mid").arg (stem, voiceName).arg (n));
        std::string why;
        if (! midifile::write (*written, file.toStdString(), why))
        {
            status->setText (tr ("The new voice could not be saved to %1: %2").arg (file, QString::fromStdString (why)));
            break;
        }
        placeGenerated (track, clip, slot, scene, written, file, trackName, tuning, target, launchAt);
        ++made;
    }
    generatedPlaced();
    const auto mode = modeChoice.system >= 0 ? tuningText (modeChoice) : tr ("the melody's mode");
    auto text = tr ("%n voice(s) of counterpoint in %1", "", made).arg (mode);
    if (made < (int) result.voices.size())
        text += tr ("; the mixer had room for %1 of %2").arg (made).arg (result.voices.size());
    if (result.bent > 0)
        text += tr ("; %n strict rule(s) bent where nothing else fitted", "", result.bent);
    status->setText (text);
}

void MixerWindow::placeGenerated (int track, const mixer::Clip& clip, mixer::SlotPtr slot, int scene, midifile::SongPtr written,
                                  const QString& file, const QString& trackName, mixer::TuningChoice tuning, int target, Frame launchAt)
{
    const bool live = launchAt >= 0;
    const auto bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    if (target < 0)
    {
        target = engine.numTracks();
        engine.setNumTracks (target + 1);
        auto& to = engine.strip (target);
        to.trimDb = 0.0f;
        to.invertLeft = to.invertRight = false;
        to.lowCut = to.highCut = false;
        to.width = 1.0f;
        to.delayMs = 0.0f;
        to.faderDb = engine.strip (track).faderDb.load();
        to.pan = 0.0f;
        to.mute = to.solo = false;
        to.transpose = 0;
        to.channels = 0xFFFF;
        engine.setInserts (target, {});
        engine.setInstrument (target, nullptr);
        engine.setClips (target, {});
        info[(size_t) target].name = trackName;
        info[(size_t) target].colour = info[(size_t) track].colour;
        info[(size_t) target].tuning = tuning;   // the PHI synth plays the new voices in the melody's tuning
    }
    if (slot != nullptr)
    {
        auto placed = std::make_shared<mixer::Slot> (*slot);
        placed->clip.midi = launcherMidi (written, file.toStdString());
        placed->clip.reversed = false;
        placed->clip.offset = std::min (clip.offset, placed->clip.midi->length);
        placed->clip.length = std::min (clip.length, placed->clip.midi->length - placed->clip.offset);
        slots[(size_t) target][(size_t) scene] = placed;
        engine.setSlot (target, scene, placed);
        if (live)
            engine.launchAt (target, scene, launchAt);
    }
    else
    {
        auto placed = mixer::makeClip (mixer::placeMidi (written, file.toStdString(), rate(), bpm), clip.start);
        placed.offset = std::min (clip.offset, placed.midi->length);
        placed.length = std::min (clip.length, placed.midi->length - placed.offset);
        editClips (target, { placed });
    }
    if (engine.instrument (target) == nullptr)
        setInstrumentKind (target, synthChoice, {});
}

void MixerWindow::generatedPlaced()
{
    rebuildStrips();
    grid->update();
    rack->refresh();
    changed();
}

bool MixerWindow::shownMidiClip (int& track, int& scene, mixer::Clip& clip, mixer::SlotPtr& slot, mixer::TuningChoice& tuning) const
{
    if (! showingClip || ! clipView->showsMidi())
        return false;
    track = clipView->shownTrack();
    scene = clipView->shownScene();
    const auto id = clipView->shownId();
    slot = nullptr;
    clip = {};
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && (slot = slots[(size_t) track][(size_t) scene]) != nullptr)
        clip = slot->clip;
    if (clip.midi == nullptr || clip.midi->song == nullptr)
        return false;
    tuning = slot != nullptr ? mixer::tuningInForce (slot->tuning, sceneTunings[(size_t) scene], info[(size_t) track].tuning)
                             : info[(size_t) track].tuning;
    return true;
}

void MixerWindow::refreshViews()
{
    if (grid != nullptr)
        grid->update();
    if (lanes != nullptr)
        lanes->update();
}

void MixerWindow::say (const QString& text)
{
    status->setText (text);
}

void MixerWindow::giveBothLayers (int track)
{
    auto& s = engine.strip (track);
    if (s.mode.load() == (int) mixer::TrackMode::hybrid)
        return;
    s.mode = (int) mixer::TrackMode::hybrid;
    strips[(size_t) track]->refresh();
    rack->refresh();
}

void MixerWindow::giveSynth (int track)
{
    if (engine.instrument (track) == nullptr)
        setInstrumentKind (track, synthChoice, {});
}

void MixerWindow::autoCounterpoint (int track, int scene, mixer::SlotPtr slot, Frame launchAt)
{
    // The Counterpoint dialog's last choices; its mode, else the track's tuning, else the melody's own.
    auto settings = appSettings();
    counterpoint::Settings options;
    options.species = (counterpoint::Species) (std::clamp (settings.value (QStringLiteral ("counterpoint/species"), 0).toInt(), 0, 4) + 1);
    options.voices = std::clamp (settings.value (QStringLiteral ("counterpoint/voices"), 1).toInt(), 1, 3);
    options.placement = (counterpoint::Placement) std::clamp (settings.value (QStringLiteral ("counterpoint/place"), 0).toInt(), 0, 2);
    options.allInSpecies = settings.value (QStringLiteral ("counterpoint/all"), false).toBool();
    options.seed = (uint32_t) settings.value (QStringLiteral ("counterpoint/variation"), 1).toInt();
    const auto tuning = mixer::tuningInForce (slot->tuning, sceneTunings[(size_t) scene], info[(size_t) track].tuning);
    mixer::TuningChoice modeChoice { mixer::findTuning (settings.value (QStringLiteral ("counterpoint/mode")).toString().toStdString()),
                                     settings.value (QStringLiteral ("counterpoint/root"), 0).toInt() };
    if (modeChoice.system < 0)
        modeChoice = tuning;
    if (modeChoice.system >= 0)
    {
        const auto& system = mixer::tuningSystems()[(size_t) modeChoice.system];
        options.mode = counterpoint::Mode::fromTuning (system, modeChoice.root);
        const auto hypo = (mixer::Hypo) settings.value (QStringLiteral ("counterpoint/hypo"), 0).toInt();
        if (mixer::hasHypo (system, hypo))
            options.mode.below = mixer::hypoBelow (hypo);
    }
    writeCounterpoint (track, slot->clip, slot, scene, options, modeChoice, tuning, launchAt);
}

void MixerWindow::refreshMidiClipBox()
{
    if (midiClipBox == nullptr)
        return;
    const bool show = showingClip && clipView->showsMidi();
    midiClipBox->setVisible (show);
    if (show)
        midiClipTitle->setText (tr ("MIDI Clip: %1").arg (clipView->shownName()));
    const bool audio = showingClip && ! clipView->showsMidi();
    audioClipBox->setVisible (audio);
    if (audio)
    {
        audioClipTitle->setText (tr ("Audio Clip: %1").arg (clipView->shownName()));
        const QSignalBlocker quiet (stretchRatioBox);
        stretchRatioBox->setValue (extremeRatio());   // the editor may have changed it
    }
}

QString MixerWindow::editsFolder() const
{
    // Beside the mix (in the Music folder until it is saved); the originals are left as they were.
    return path.isEmpty() ? QDir (QStandardPaths::writableLocation (QStandardPaths::MusicLocation)).filePath (tr ("PHI Edits"))
                          : QFileInfo (path).absoluteDir().filePath (QFileInfo (path).completeBaseName() + tr (" Edits"));
}

void MixerWindow::reverseShownClip()
{
    if (! showingClip)
        return;
    const auto track = clipView->shownTrack();
    if (const auto id = clipView->shownId(); id != 0)
    {
        remember();
        auto list = engine.clips (track);
        for (auto& c : list)
            if (c.id == id)
            {
                c.offset = c.sourceLength() - c.offset - c.length;
                c.reversed = ! c.reversed;
                if (c.midi != nullptr)
                    c.midi = mixer::reverseMidi (*c.midi);
            }
        editClips (track, std::move (list));
    }
    else
        editSlot (track, clipView->shownScene(), [] (mixer::Slot& x)
        {
            x.clip.reversed = ! x.clip.reversed;
            if (x.clip.midi != nullptr)
                x.clip.midi = mixer::reverseMidi (*x.clip.midi);
        });
    status->setText (tr ("Reversed (R again turns it back)"));
    changed();
}

namespace layers
{
    std::shared_ptr<mixer::AudioClip> flatAudio (const mixer::Clip&);   // MixerRecording.cpp
}

bool MixerWindow::showsMidiClip() const
{
    return showingClip && clipView->showsMidi();
}

bool MixerWindow::modeShownMidi (int root, uint16_t keys, int below, const QString& what)
{
    if (! showsMidiClip())
        return false;
    editShownMidi ([root, keys, below] (const midifile::Song& s)
                   { return below > 0 ? midifile::inRange (*midifile::toKeys (s, root, keys), root, below) : midifile::toKeys (s, root, keys); },
                   what);
    return true;
}

bool MixerWindow::tuneSelectedTrack (mixer::TuningChoice choice)
{
    if (selected >= mixer::masterIndex || selected >= engine.numTracks())
        return false;
    info[(size_t) selected].tuning = choice;
    changed();
    applyTunings();
    if (inspectorStrip != nullptr)
        inspectorStrip->refresh();
    if (auto* strip = strips[(size_t) selected])
        strip->refresh();
    say (tr ("%1 tuned to %2").arg (trackName (selected), tuningText (choice)));
    return true;
}

bool MixerWindow::addRhythm (const midifile::Song& song, const QString& stem)
{
    if (selected >= mixer::masterIndex || selected >= engine.numTracks())
        return false;
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto name = stem;
    name.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    QString file;
    for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
        file = QDir (base).filePath (QStringLiteral ("%1 %2.mid").arg (name).arg (n));
    std::string why;
    if (! midifile::write (song, file.toStdString(), why))
    {
        say (tr ("The rhythm could not be saved to %1: %2").arg (file, QString::fromStdString (why)));
        return true;
    }
    addFiles (selected, { file }, engine.position());
    return true;
}

void MixerWindow::editShownMidi (const std::function<midifile::SongPtr (const midifile::Song&)>& edit, const QString& what)
{
    if (! showingClip || ! clipView->showsMidi())
        return;
    const auto track = clipView->shownTrack();
    const auto id = clipView->shownId();
    const auto scene = clipView->shownScene();
    mixer::Clip clip;
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && slots[(size_t) track][(size_t) scene] != nullptr)
        clip = slots[(size_t) track][(size_t) scene]->clip;
    if (clip.midi == nullptr || clip.midi->song == nullptr)
        return;
    auto song = clip.midi->song;
    if (song->ticksPerQuarter <= 0)
    {
        status->setText (tr ("This MIDI file counts in SMPTE time, so it cannot be edited here."));
        return;
    }
    // Edited as it is heard: a reversed clip is turned round first, and stays that way.
    if (clip.reversed)
        song = midifile::reversed (*song);
    const auto edited = edit (*song);

    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName();
    if (stem.isEmpty())
        stem = tr ("MIDI");
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    stem.remove (QRegularExpression (QStringLiteral (" \\d+$")));   // "riff 3" becomes "riff 4", not "riff 3 2"
    QString file;
    for (int n = 2; file.isEmpty() || QFileInfo::exists (file); ++n)
        file = QDir (base).filePath (QStringLiteral ("%1 %2.mid").arg (stem).arg (n));
    std::string why;
    if (! midifile::write (*edited, file.toStdString(), why))
    {
        status->setText (tr ("The edit could not be saved to %1: %2").arg (file, QString::fromStdString (why)));
        return;
    }
    const auto bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    if (id != 0)
    {
        remember();
        auto list = engine.clips (track);
        for (auto& c : list)
            if (c.id == id)
            {
                c.midi = mixer::placeMidi (edited, file.toStdString(), rate(), bpm);
                c.offset = 0;
                c.length = c.midi->length;
                c.reversed = false;
            }
        editClips (track, std::move (list));
        status->setText (tr ("%1 (Ctrl+Z puts it back)").arg (what));
    }
    else
    {
        editSlot (track, scene, [this, &edited, &file] (mixer::Slot& x)
        {
            // Only the MIDI layer changes: an audio layer under it keeps the loop's length.
            if (x.clip.audio != nullptr && (x.clip.offset != 0 || x.clip.reversed))
                x.clip.audio = layers::flatAudio (x.clip);
            x.clip.midi = launcherMidi (edited, file.toStdString());
            x.clip.length = x.clip.audio != nullptr ? std::min (x.clip.length, x.clip.audio->length()) : x.clip.midi->length;
            x.clip.offset = 0;
            x.clip.reversed = false;
        });
        status->setText (what);
    }
    changed();
}

bool MixerWindow::devicePanelShown() const { return devicePanel != nullptr && ! devicePanel->isHidden(); }

void MixerWindow::updateDevicePanel()
{
    if (devicePanel == nullptr)
        return;
    refreshMidiClipBox();
    const bool show = true;   // fixed along the bottom; the master's shows nothing to change
    if (devicePanel->isHidden() == show)
    {
        devicePanel->setVisible (show);
        Q_EMIT devicePanelChanged();
    }
}

void MixerWindow::showDevices()
{
    showingClip = false;
    devicePanel->setCurrentWidget (devices);
    devices->refresh();
    updateDevicePanel();
}

void MixerWindow::showTimelineClip (int track, uint32_t id)
{
    if (track != selected)
        select (track);
    showingClip = true;
    clipView->set (track, id, -1);
    devicePanel->setCurrentWidget (clipView);
    updateDevicePanel();
}

void MixerWindow::showSlotClip (int track, int scene)
{
    if (track != selected)
        select (track);
    showingClip = true;
    clipView->set (track, 0, scene);
    devicePanel->setCurrentWidget (clipView);
    updateDevicePanel();
}

bool MixerWindow::dropPlugin (int track, const QByteArray& what)
{
    if (track < 0 || track >= engine.numTracks())
        return false;
    const auto parts = QString::fromUtf8 (what).split (QLatin1Char ('\n'));
    const auto kind = parts.value (0);
    const auto id = parts.value (1);
    select (track);
    if (kind == QLatin1String ("effect"))
    {
        if ((int) engine.inserts (track).size() >= mixer::maxInserts)
        {
            status->setText (tr ("Track %1 has all %2 inserts in use.").arg (track + 1).arg (mixer::maxInserts));
            return false;
        }
        addInsert (track, id.toStdString());
        showDevices();
        return true;
    }
    const int choice = kind == QLatin1String ("synth") ? synthChoice : kind == QLatin1String ("sampler") ? samplerChoice
                     : kind == QLatin1String ("slicer") ? slicerChoice : kind == QLatin1String ("raga") ? ragaChoice
                     : kind == QLatin1String ("instrument") ? firstPlugin : -1;
    if (choice < 0)
        return false;
    // An audio track with an instrument dropped on it plays it: as a hybrid when it has audio.
    auto& s = engine.strip (track);
    if (s.mode.load() == (int) mixer::TrackMode::audio)
    {
        bool audioClips = false;
        for (const auto& c : engine.clips (track))
            audioClips = audioClips || c.audio != nullptr;
        for (const auto& slot : slots[(size_t) track])
            audioClips = audioClips || (slot != nullptr && slot->clip.audio != nullptr);
        s.mode = (int) (audioClips ? mixer::TrackMode::hybrid : mixer::TrackMode::instrument);
    }
    setInstrumentKind (track, choice, id);
    showDevices();
    changed();
    return true;
}

void MixerWindow::select (int index)
{
    if (index < mixer::masterIndex)
        index = std::clamp (index, 0, engine.numTracks() - 1);
    selected = index;
    if (index < mixer::masterIndex && index != shownTrack)
    {
        shownTrack = index;
        refreshEditorTrack();
    }
    if (! picked.count (index))
        picked = { index };
    // The clip shown belongs to the track that was selected; this one shows its devices.
    if (showingClip && clipView->shownTrack() != index)
    {
        showingClip = false;
        devicePanel->setCurrentWidget (devices);
    }
    updateDevicePanel();
    for (int i = 0; i < mixer::maxTracks; ++i)
        strips[(size_t) i]->setSelected (i == selected, picked.count (i) > 0);
    if (lanes != nullptr)
        lanes->update();
    if (browser != nullptr)
        browser->showHint();
    if (rack != nullptr)
        rack->refresh();
    refreshInspector();
    if (grid != nullptr)
        grid->update();
    masterStrip->setStyleSheet (selected == mixer::masterIndex
                                    ? QStringLiteral ("QFrame#masterStrip { border: 2px solid %1; }").arg (Theme::current().color (QStringLiteral ("highlight")).name())
                                    : QString());
}

void MixerWindow::tick()
{
    applyTunings();   // launches happen with the mixer hidden too
    keepRecorded();   // and takes end
    if (! isVisible())
        return;   // closed, or docked behind the editor: nothing to draw
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto seconds = std::clamp ((double) (now - lastTick) / 1000.0, 0.0, 0.5);
    lastTick = now;
    for (int i = 0; i < engine.numTracks(); ++i)
        strips[(size_t) i]->tick (seconds);
    for (auto* r : returnStrips)
        r->tick (seconds);
    masterMeter->setLevels (engine.takeMeter (mixer::masterIndex), seconds);
    masterPeak->setText (peakText (masterMeter->highestPeak()));
    refreshTransport();
    if (lanes->isVisible())
    {
        lanes->follow();
        lanes->update();
    }
    if (launcherArea->isVisible())
    {
        grid->update();
        rack->tick();
    }
}

void MixerWindow::refreshTransport()
{
    // In the launcher, while clips play or record, the launch clock; else the timeline.
    bool clocked = false;
    if (launcherShown())
        for (int t = 0; t < engine.numTracks() && ! clocked; ++t)
            clocked = engine.launchInfo (t).playing >= 0 || (engine.recordInfo (t).state != mixer::RecordState::none && engine.recordInfo (t).scene >= 0);
    showPosition (clocked ? engine.launchClock() : engine.position(), clocked);
    static_cast<TransportButton*> (playButton)->setSymbol (engine.isPlaying() || engine.countingIn() ? TransportButton::Symbol::pause
                                                                                                     : TransportButton::Symbol::play);
}

void MixerWindow::togglePlay()
{
    if (engine.isPlaying() || engine.countingIn())
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

void MixerWindow::decodeAudio (int track, const QString& file, std::function<void (mixer::AudioClipPtr)> done)
{
    auto& t = info[(size_t) track];
    const auto generation = t.generation;
    const auto r = rate();
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());

    // A file already on a track at this rate is shared rather than read again.
    if (auto cached = audioCache[file].lock(); cached != nullptr)
    {
        done (cached);
        return;
    }

    ++t.busy;
    lanes->update();
    status->setText (tr ("Loading %1...").arg (QFileInfo (file).fileName()));
    QPointer<MixerWindow> self (this);
    (void) QtConcurrent::run ([self, track, generation, file, r, done]() mutable
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
        QMetaObject::invokeMethod (qApp, [self, track, generation, file, clip, error, done]() mutable
        {
            if (self == nullptr || self->info[(size_t) track].generation != generation)
                return;
            auto& loading = self->info[(size_t) track];
            loading.busy = std::max (0, loading.busy - 1);
            if (clip == nullptr)
            {
                self->status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (error)));
                self->lanes->update();
                done (nullptr);
                return;
            }
            self->audioCache[file] = clip;
            done (clip);
        }, Qt::QueuedConnection);
    });
}

void MixerWindow::loadAudio (int track, const QString& file, mixer::Clip place, std::function<void (Frame)> then)
{
    // One that a project being opened put there is not a change to it, however late it lands.
    decodeAudio (track, file, [this, track, file, place, then, fromFile = restoring] (mixer::AudioClipPtr clip) mutable
    {
        if (clip == nullptr)
            return;
        place.audio = clip;
        if (place.length <= 0)
            place.length = clip->length();
        const auto keep = std::exchange (restoring, restoring || fromFile);
        addClip (track, place);
        restoring = keep;
        status->setText (tr ("%1 is on track %2 at %3").arg (QFileInfo (file).fileName()).arg (track + 1).arg (timeText ((double) place.start / rate())));
        if (then)
            then (place.end());
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
    placeLauncherMidi();
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
        case ragaChoice: made = std::make_shared<mixer::RagaInstrument>(); break;
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
                                                      : (hooks.slicerSound ? slicerHasSound (hooks.slicerSound()) : false);
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
        if (! slicerHasSound ({ sound, kit }))
        {
            status->setText (tr ("The Slicer has no sound yet: send one with F8 first, or drop sounds on its pads."));
            return;
        }
        auto& s = static_cast<mixer::SlicerInstrument&> (*inst);
        s.engine().setSample (sound);
        s.engine().setKit (kit);
        s.samplePath.clear();
        info[(size_t) track].sound = sound != nullptr ? soundAsAudio (*sound, rate()) : nullptr;
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
            auto* d = synthDialog (inst, title, this, [this] { changed(); });
            t.editor = d;
            break;
        }
        case mixer::Instrument::Kind::raga:
        {
            auto* d = ragaDialog (inst, title, this, [this] { changed(); });
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
    updateInput();   // an audio track armed hears its input
    for (int i = 0; i < mixer::maxTracks; ++i)
        strips[(size_t) i]->armButton->setChecked (engine.liveTrack() == i);
    const auto inst = engine.liveTrack() >= 0 ? engine.instrument (engine.liveTrack()) : nullptr;
    status->setText (engine.liveTrack() < 0 ? tr ("No track gets the MIDI keyboard.")
                     : inst == nullptr ? tr ("Track %1 gets the MIDI keyboard, but has no instrument yet.").arg (track + 1)
                                       : tr ("The MIDI keyboard plays track %1's %2.").arg (track + 1).arg (QString::fromStdString (inst->name())));
}

//==============================================================================
// The knobs of an effect without a window of its own (the built-in and Φ effects): a slider
// for each, its value beside it. Deletes itself when closed; keeps the effect alive meanwhile.
static QDialog* effectDialog (mixer::EffectPtr effect, const QString& title, QWidget* parent, std::function<void()> changed)
{
    auto* d = new QDialog (parent);
    d->setAttribute (Qt::WA_DeleteOnClose);
    d->setWindowTitle (title);
    auto* form = new QFormLayout (d);
    const auto params = effect->parameters();
    if (params.empty())
        form->addRow (new QLabel (QObject::tr ("This effect has no settings."), d));
    std::vector<QLabel*> values;
    for (size_t k = 0; k < params.size(); ++k)
    {
        const auto& p = params[k];
        auto* value = new QLabel (QString::fromStdString (p.text), d);
        value->setMinimumWidth (70);
        values.push_back (value);
        auto* row = new QHBoxLayout;
        if (p.toggle)
        {
            auto* box = new QCheckBox (d);
            box->setChecked (p.value >= 0.5f);
            QObject::connect (box, &QCheckBox::toggled, d, [=] (bool on)
            {
                effect->setParameter ((int) k, on ? 1.0f : 0.0f);
                value->setText (QString::fromStdString (effect->parameters()[k].text));
                changed();
            });
            row->addWidget (box, 1);
        }
        else
        {
            auto* slider = new QSlider (Qt::Horizontal, d);
            const int steps = p.steps > 1 ? p.steps - 1 : 1000;
            slider->setRange (0, steps);
            slider->setValue ((int) std::lround (p.value * steps));
            slider->setMinimumWidth (180);
            QObject::connect (slider, &QSlider::valueChanged, d, [=] (int v)
            {
                effect->setParameter ((int) k, (float) v / (float) steps);
                value->setText (QString::fromStdString (effect->parameters()[k].text));
                changed();
            });
            row->addWidget (slider, 1);
        }
        row->addWidget (value);
        form->addRow (QString::fromStdString (p.name), row);
    }
    auto keep = std::make_shared<mixer::EffectPtr> (effect);
    QObject::connect (d, &QObject::destroyed, d, [keep] { keep->reset(); });
    return d;
}

void MixerWindow::chooseInsert (int track, int replace, QPoint at)
{
    if (replace < 0 && (int) engine.inserts (track).size() >= mixer::maxInserts)
    {
        status->setText (tr ("Track %1 has all %2 inserts in use.").arg (track + 1).arg (mixer::maxInserts));
        return;
    }
    QMenu menu (this);
    auto plugins = audio.knownPlugins();
    std::sort (plugins.begin(), plugins.end(), [] (const PluginInfo& x, const PluginInfo& y)
    {
        return QString::fromStdString (x.name).compare (QString::fromStdString (y.name), Qt::CaseInsensitive) < 0;
    });
    // By format (Built-in, Stock, VST3, LV2...), the built-in ones first.
    std::map<QString, QMenu*> byFormat;
    auto formatMenu = [&] (const QString& format)
    {
        auto& m = byFormat[format];
        if (m == nullptr)
            m = new QMenu (format, &menu);
        return m;
    };
    for (const auto& p : plugins)
    {
        const auto id = p.id;
        formatMenu (QString::fromStdString (p.format))->addAction (QString::fromStdString (p.name), this, [this, track, id, replace] { addInsert (track, id, replace); });
    }
    if (auto it = byFormat.find (QStringLiteral ("Built-in")); it != byFormat.end())
        menu.addMenu (it->second);
    for (const auto& [format, sub] : byFormat)
        if (format != QLatin1String ("Built-in"))
            menu.addMenu (sub);
    if (plugins.empty())
        menu.addAction (tr ("No effects found (Master Section > Manage Plug-ins scans for them)"))->setEnabled (false);
    menu.exec (at.isNull() ? QCursor::pos() : at);
}

void MixerWindow::addInsert (int track, const std::string& pluginId, int replace)
{
    std::string error;
    auto made = audio.loadEffectPlugin (pluginId, rate(), error);
    if (made == nullptr)
    {
        QMessageBox::warning (this, tr ("Inserts"), tr ("The effect could not be started: %1").arg (QString::fromStdString (error)));
        return;
    }
    auto list = engine.inserts (track);
    if (replace >= 0 && replace < (int) list.size())
        list[(size_t) replace] = { made, false };
    else
        list.push_back ({ made, false });
    engine.setInserts (track, std::move (list));
    strips[(size_t) track]->refreshInserts();
    changed();
    status->setText (tr ("%1 is on track %2 (double-click it to open it).").arg (QString::fromStdString (made->name())).arg (track + 1));
}

void MixerWindow::editInsert (int track, int insert)
{
    const auto list = engine.inserts (track);
    if (insert < 0 || insert >= (int) list.size())
        return;
    const auto effect = list[(size_t) insert].effect;
    if (effect->hasWindow())
    {
        effect->showWindow();
        changed();
        return;
    }
    auto* d = effectDialog (effect, tr ("%1: %2").arg (info[(size_t) track].name, QString::fromStdString (effect->name())), this, [this] { changed(); });
    d->show();
    d->raise();
    d->activateWindow();
}

void MixerWindow::insertMenu (int track, int insert, QPoint at)
{
    const auto list = engine.inserts (track);
    QMenu menu (this);
    const bool full = (int) list.size() >= mixer::maxInserts;
    if (insert >= 0 && insert < (int) list.size())
    {
        const auto& x = list[(size_t) insert];
        menu.addAction (tr ("&Open %1").arg (QString::fromStdString (x.effect->name())), this, [this, track, insert] { editInsert (track, insert); });
        auto* bypass = menu.addAction (tr ("&Bypass"));
        bypass->setCheckable (true);
        bypass->setChecked (x.bypassed);
        connect (bypass, &QAction::toggled, this, [this, track, insert] (bool on) { setInsertBypassed (track, insert, on); });
        menu.addAction (tr ("&Mix: %1%...").arg ((int) std::lround (x.mix * 100.0f)), this, [this, track, insert, mix = x.mix]
        {
            bool ok = false;
            const auto v = QInputDialog::getInt (this, tr ("Insert Mix"), tr ("How much of the effect, the rest the dry sound (parallel processing), %:"),
                                                 (int) std::lround (mix * 100.0f), 0, 100, 5, &ok);
            if (ok)
                setInsertMix (track, insert, (float) v / 100.0f);
        });
        menu.addAction (tr ("Move &Up"), this, [this, track, insert] { moveInsert (track, insert, insert - 1); })->setEnabled (insert > 0);
        menu.addAction (tr ("Move &Down"), this, [this, track, insert] { moveInsert (track, insert, insert + 1); })->setEnabled (insert + 1 < (int) list.size());
        menu.addAction (tr ("&Replace..."), this, [this, track, insert, at] { chooseInsert (track, insert, at); });
        menu.addAction (tr ("&Remove"), this, [this, track, insert] { removeInsert (track, insert); });
        menu.addSeparator();
    }
    menu.addAction (tr ("&Add Effect..."), this, [this, track, at] { chooseInsert (track, -1, at); })->setEnabled (! full);
    const bool anyOn = std::any_of (list.begin(), list.end(), [] (const mixer::Insert& x) { return ! x.bypassed; });
    menu.addAction (anyOn ? tr ("Bypass &All") : tr ("Turn &All On"), this, [this, track, anyOn]
    {
        auto all = engine.inserts (track);
        for (auto& x : all)
            x.bypassed = anyOn;
        engine.setInserts (track, std::move (all));
        strips[(size_t) track]->refreshInserts();
        changed();
    })->setEnabled (! list.empty());
    menu.exec (at);
}

void MixerWindow::setInsertBypassed (int track, int insert, bool on)
{
    auto list = engine.inserts (track);
    if (insert < 0 || insert >= (int) list.size())
        return;
    list[(size_t) insert].bypassed = on;
    engine.setInserts (track, std::move (list));
    strips[(size_t) track]->refreshInserts();
    changed();
}

void MixerWindow::setInsertMix (int track, int insert, float mix)
{
    auto list = engine.inserts (track);
    if (insert < 0 || insert >= (int) list.size())
        return;
    list[(size_t) insert].mix = std::clamp (mix, 0.0f, 1.0f);
    engine.setInserts (track, std::move (list));
    strips[(size_t) track]->refreshInserts();
    changed();
}

void MixerWindow::removeInsert (int track, int insert)
{
    auto list = engine.inserts (track);
    if (insert < 0 || insert >= (int) list.size())
        return;
    list.erase (list.begin() + insert);
    engine.setInserts (track, std::move (list));
    strips[(size_t) track]->refreshInserts();
    changed();
}

void MixerWindow::moveInsert (int track, int insert, int to)
{
    auto list = engine.inserts (track);
    if (insert < 0 || insert >= (int) list.size() || to < 0 || to >= (int) list.size() || to == insert)
        return;
    const auto x = list[(size_t) insert];
    list.erase (list.begin() + insert);
    list.insert (list.begin() + to, x);
    engine.setInserts (track, std::move (list));
    strips[(size_t) track]->refreshInserts();
    changed();
}

void MixerWindow::reorderInserts (int track, const std::vector<int>& order)
{
    const auto list = engine.inserts (track);
    mixer::Inserts now;
    for (const auto k : order)
        if (k >= 0 && k < (int) list.size())
            now.push_back (list[(size_t) k]);
    if (now.size() == list.size())
    {
        engine.setInserts (track, std::move (now));
        changed();
    }
    strips[(size_t) track]->refreshInserts();
}

QJsonArray MixerWindow::insertsJson (int track) const
{
    QJsonArray list;
    for (const auto& x : engine.inserts (track))
        list.append (QJsonObject {
            { "id", QString::fromStdString (x.effect->pluginId()) }, { "name", QString::fromStdString (x.effect->name()) },
            { "state", QString::fromStdString (x.effect->state()) }, { "bypassed", x.bypassed }, { "mix", x.mix },
        });
    return list;
}

void MixerWindow::loadInserts (int track, const QJsonArray& list, QStringList& problems)
{
    mixer::Inserts made;
    for (const auto& value : list)
    {
        const auto o = value.toObject();
        std::string error;
        auto effect = audio.loadEffectPlugin (o.value ("id").toString().toStdString(), rate(), error);
        if (effect == nullptr)
        {
            problems << tr ("Track %1: the insert %2 could not be started (%3)").arg (track + 1).arg (o.value ("name").toString(), QString::fromStdString (error));
            continue;
        }
        effect->setState (o.value ("state").toString().toStdString());
        made.push_back ({ effect, o.value ("bypassed").toBool(), (float) std::clamp (o.value ("mix").toDouble (1.0), 0.0, 1.0) });
    }
    engine.setInserts (track, std::move (made));
}

void MixerWindow::stripsMenu (QPoint at)
{
    QMenu menu (this);
    const std::pair<int, QString> sections[] = {
        { instrumentSection, tr ("&Instrument and MIDI") }, { preSection, tr ("&Pre: Polarity, Cuts and Delay") },
        { insertSection, tr ("I&nserts") }, { sendSection, tr ("S&ends") }, { panSection, tr ("&Trim, Pan and Width") },
    };
    auto apply = [this]
    {
        appSettings().setValue (QStringLiteral ("mixer/stripSections"), stripSections);
        appSettings().setValue (QStringLiteral ("mixer/wideStrips"), wideStrips);
        for (auto* strip : strips)
            strip->applyLayout();
        refreshInspector();
    };
    for (const auto& [bit, text] : sections)
    {
        auto* a = menu.addAction (text);
        a->setCheckable (true);
        a->setChecked ((stripSections & bit) != 0);
        connect (a, &QAction::toggled, this, [this, bit, apply] (bool on)
        {
            stripSections = on ? (stripSections | bit) : (stripSections & ~bit);
            apply();
        });
    }
    menu.addAction (tr ("&Show All"), this, [this, apply] { stripSections = allSections; apply(); });
    menu.addAction (tr ("Only &Faders"), this, [this, apply] { stripSections = 0; apply(); });
    menu.addSeparator();
    auto* wide = menu.addAction (tr ("&Wide Strips"));
    wide->setCheckable (true);
    wide->setChecked (wideStrips);
    connect (wide, &QAction::toggled, this, [this, apply] (bool on) { wideStrips = on; apply(); });
    menu.exec (at);
}

QString MixerWindow::applyEqCuts (int track, const std::vector<std::pair<double, double>>& cuts)
{
    const auto phiEq = QString::fromUtf8 ("Φ EQ");
    std::string id;
    for (const auto& p : audio.knownPlugins())
        if (QString::fromStdString (p.name) == phiEq)
            id = p.id;
    if (id.empty())
        return tr ("The Φ EQ is not among the plugins (Manage Plug-ins may have turned it off).");
    auto list = engine.inserts (track);
    auto it = std::find_if (list.begin(), list.end(), [&] (const mixer::Insert& x) { return x.effect->pluginId() == id; });
    if (it == list.end())
    {
        if ((int) list.size() >= mixer::maxInserts)
            return tr ("%1 has all its inserts in use.").arg (info[(size_t) track].name);
        std::string error;
        auto made = audio.loadEffectPlugin (id, rate(), error);
        if (made == nullptr)
            return tr ("The Φ EQ could not be started: %1").arg (QString::fromStdString (error));
        list.push_back ({ made, false });
        it = list.end() - 1;
    }
    it->bypassed = false;
    const auto effect = it->effect;
    engine.setInserts (track, std::move (list));
    // Each cut on the band nearest to it (the bands are a golden ratio apart), the deepest kept.
    std::array<double, phi::Eq::numBands> gains {};
    const auto params = effect->parameters();
    for (int b = 0; b < phi::Eq::numBands && b < (int) params.size(); ++b)
        gains[(size_t) b] = params[(size_t) b].value * 24.0 - 12.0;
    for (const auto& [hz, db] : cuts)
    {
        int nearest = 0;
        for (int b = 1; b < phi::Eq::numBands; ++b)
            if (std::abs (std::log (phi::Eq::bandHz (b) / hz)) < std::abs (std::log (phi::Eq::bandHz (nearest) / hz)))
                nearest = b;
        gains[(size_t) nearest] = std::min (gains[(size_t) nearest], std::clamp (db, -12.0, 0.0));
    }
    for (int b = 0; b < phi::Eq::numBands; ++b)
        effect->setParameter (b, (float) ((gains[(size_t) b] + 12.0) / 24.0));
    strips[(size_t) track]->refreshInserts();
    changed();
    return {};
}

void MixerWindow::forLinked (int track, const std::function<void (mixer::Strip&, bool self)>& fn)
{
    fn (engine.strip (track), true);
    std::set<int> others;
    const auto mods = QApplication::keyboardModifiers();
    if ((mods & (Qt::AltModifier | Qt::ShiftModifier)) == (Qt::AltModifier | Qt::ShiftModifier))
        for (int i = 0; i < engine.numTracks(); ++i)
            others.insert (i);
    else if (qLinkButton != nullptr && qLinkButton->isChecked() && picked.count (track))
        others = picked;
    for (const auto i : others)
        if (i != track && i < engine.numTracks())
        {
            fn (engine.strip (i), false);
            strips[(size_t) i]->refresh();
        }
    changed();
}

void MixerWindow::pick (int track, bool add)
{
    if (! add)
    {
        select (track);
        return;
    }
    if (picked.count (track) && picked.size() > 1)
    {
        picked.erase (track);
        select (track == selected ? *picked.begin() : selected);
    }
    else
    {
        picked.insert (track);
        select (track);
    }
    if (picked.size() > 1 && ! qLinkButton->isChecked())
        status->setText (tr ("%1 strips picked: turn on Q-Link to change them together.").arg (picked.size()));
}

namespace
{
    // Sets an effect's knob by its name, as text in its own units ("1.8", "100").
    void setKnob (const mixer::EffectPtr& fx, const char* name, double value)
    {
        const auto params = fx->parameters();
        for (size_t k = 0; k < params.size(); ++k)
            if (params[k].name == name)
                fx->setParameterText ((int) k, QString::number (value).toStdString());
    }
}

void MixerWindow::defaultReturns (int only)
{
    std::string space, echo;
    for (const auto& p : audio.knownPlugins())
    {
        const auto name = QString::fromStdString (p.name);
        if (name == QString::fromUtf8 ("Φ Space"))
            space = p.id;
        else if (name == QString::fromUtf8 ("Φ Echo"))
            echo = p.id;
    }
    for (int r = 0; r < mixer::numReturns; ++r)
        if (only < 0 || only == r)
        {
            auto& c = engine.returnStrip (r);
            c.faderDb = 0.0f;
            c.pan = 0.0f;
            c.mute = false;
            setReturnEffect (r, r == 0 ? space : echo);
        }
    // A: a plate-like 1.8 s; B: a dotted eighth at the tempo, 30 % feedback; both fully wet.
    if (auto a = engine.returnEffect (0); a != nullptr && only != 1)
    {
        setKnob (a, "Decay", 1.8);
        setKnob (a, "Mix", 100.0);
    }
    if (auto b = engine.returnEffect (1); b != nullptr && only != 0)
    {
        setKnob (b, "Time", 0.75 * 60000.0 / std::max (20.0, tempoBox->value()));
        setKnob (b, "Taps", 1.0);
        setKnob (b, "Feedback", 30.0);
        setKnob (b, "Mix", 100.0);
    }
    for (auto* r : returnStrips)
        if (r != nullptr)
            r->refresh();
}

void MixerWindow::setReturnEffect (int index, const std::string& pluginId)
{
    mixer::EffectPtr made;
    if (! pluginId.empty())
    {
        std::string error;
        made = audio.loadEffectPlugin (pluginId, rate(), error);
        if (made == nullptr)
            status->setText (tr ("The return's effect could not be started: %1").arg (QString::fromStdString (error)));
    }
    engine.setReturnEffect (index, made);
    if (returnStrips[(size_t) index] != nullptr)
        returnStrips[(size_t) index]->refresh();
    changed();
}

void MixerWindow::chooseReturnEffect (int index, QPoint at)
{
    QMenu menu (this);
    menu.addAction (tr ("No Effect (the sends as they are)"), this, [this, index] { setReturnEffect (index, {}); });
    menu.addAction (index == 0 ? tr ("Back to the Default (Φ Space)") : tr ("Back to the Default (Φ Echo)"), this, [this, index] { defaultReturns (index); });
    menu.addSeparator();
    auto plugins = audio.knownPlugins();
    std::sort (plugins.begin(), plugins.end(), [] (const PluginInfo& x, const PluginInfo& y)
    {
        return QString::fromStdString (x.name).compare (QString::fromStdString (y.name), Qt::CaseInsensitive) < 0;
    });
    std::map<QString, QMenu*> byFormat;
    for (const auto& p : plugins)
    {
        auto& sub = byFormat[QString::fromStdString (p.format)];
        if (sub == nullptr)
            sub = new QMenu (QString::fromStdString (p.format), &menu);
        const auto id = p.id;
        sub->addAction (QString::fromStdString (p.name), this, [this, index, id] { setReturnEffect (index, id); });
    }
    for (const auto& [format, sub] : byFormat)
        menu.addMenu (sub);
    menu.exec (at);
}

void MixerWindow::editReturn (int index)
{
    const auto fx = engine.returnEffect (index);
    if (fx == nullptr)
    {
        chooseReturnEffect (index, QCursor::pos());
        return;
    }
    if (fx->hasWindow())
    {
        fx->showWindow();
        return;
    }
    auto* d = effectDialog (fx, index == 0 ? tr ("A Reverb: %1").arg (QString::fromStdString (fx->name()))
                                           : tr ("B Delay: %1").arg (QString::fromStdString (fx->name())), this, [this] { changed(); });
    d->show();
    d->raise();
    d->activateWindow();
}

QJsonArray MixerWindow::returnsJson() const
{
    QJsonArray list;
    for (int r = 0; r < mixer::numReturns; ++r)
    {
        auto& c = engine.returnStrip (r);
        const auto fx = engine.returnEffect (r);
        QJsonObject o { { "fader", c.faderDb.load() }, { "pan", c.pan.load() }, { "mute", c.mute.load() } };
        if (fx != nullptr)
            o.insert ("effect", QJsonObject { { "id", QString::fromStdString (fx->pluginId()) }, { "name", QString::fromStdString (fx->name()) },
                                              { "state", QString::fromStdString (fx->state()) } });
        list.append (o);
    }
    return list;
}

void MixerWindow::loadReturns (const QJsonArray& list, QStringList& problems)
{
    for (int r = 0; r < mixer::numReturns && r < list.size(); ++r)
    {
        const auto o = list[r].toObject();
        auto& c = engine.returnStrip (r);
        c.faderDb = (float) o.value ("fader").toDouble();
        c.pan = (float) o.value ("pan").toDouble();
        c.mute = o.value ("mute").toBool();
        const auto e = o.value ("effect").toObject();
        mixer::EffectPtr made;
        if (! e.isEmpty())
        {
            std::string error;
            made = audio.loadEffectPlugin (e.value ("id").toString().toStdString(), rate(), error);
            if (made == nullptr)
                problems << tr ("Return %1: %2 could not be started (%3)").arg (QChar ('A' + r)).arg (e.value ("name").toString(), QString::fromStdString (error));
            else
                made->setState (e.value ("state").toString().toStdString());
        }
        engine.setReturnEffect (r, made);
    }
}

void MixerWindow::stripsChanged()
{
    for (int i = 0; i < engine.numTracks(); ++i)
        strips[(size_t) i]->refresh();
    changed();
}

void MixerWindow::changed()
{
    refreshInspector();   // and the selected strip: either may have made the change
    if (restoring)
        return;
    modified = true;
    updateTitle();
}

std::pair<QString, QColor> MixerWindow::defaultTrack (int track) const
{
    QStringList names;
    QList<QColor> colours;
    if (hooks.selectorSet)
        hooks.selectorSet (names, colours);
    // Bottom up: the set's last category is track 1.
    const auto k = (int) std::min (names.size(), colours.size()) - 1 - track;
    if (k >= 0 && k < (int) names.size())
        return { names[k], colours[k] };
    return { tr ("Track %1").arg (track + 1), trackColours[std::clamp (track, 0, mixer::maxTracks - 1)] };
}

void MixerWindow::applySelectorSet()
{
    bool any = false;
    for (int i = 0; i < mixer::maxTracks; ++i)
    {
        auto& t = info[(size_t) i];
        const auto plain = std::make_pair (tr ("Track %1").arg (i + 1), trackColours[i]);
        const auto given = givenDefaults[(size_t) i];
        const auto now = defaultTrack (i);
        // Only a track that still has what it was given (or the plain "Track n") follows.
        if ((t.name == given.first && t.colour == given.second) || (t.name == plain.first && t.colour == plain.second))
        {
            any = any || t.name != now.first || t.colour != now.second;
            t.name = now.first;
            t.colour = now.second;
        }
        givenDefaults[(size_t) i] = now;
    }
    if (! any)
        return;
    for (auto* strip : strips)
        strip->refresh();
    lanes->update();
    grid->update();
    rack->refresh();
}

QString MixerWindow::trackName (int track) const
{
    return track >= 0 && track < mixer::maxTracks ? info[(size_t) track].name : QString();
}

QString MixerWindow::mixName() const
{
    return path.isEmpty() ? tr ("Untitled project") : QFileInfo (path).completeBaseName();
}

void MixerWindow::updateTitle()
{
    setWindowTitle (tr ("Mixer: %1%2").arg (mixName(), modified ? QStringLiteral (" *") : QString()));
}

//==============================================================================
void MixerWindow::keyPressEvent (QKeyEvent* e)
{
    if (launcherShown() && launcherKey (e))
        return;
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
            case Qt::Key_F:
                if (! withShift)
                {
                    toggleBrowser();
                    return;
                }
                break;
            case Qt::Key_Y: redo(); return;
            case Qt::Key_R: withShift ? captureMidi() : recordPressed(); return;
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
            {
                select (key - Qt::Key_1);
                engageTrack();
            }
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
            case Qt::Key_Insert:
                if (! master)
                {
                    auto* list = strips[(size_t) selected]->insertList;
                    chooseInsert (selected, -1, list->mapToGlobal (QPoint (0, list->height())));
                }
                return;
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
            case Qt::Key_End: engine.setPosition (engine.length()); refreshTransport(); return;
            case Qt::Key_L: loopButton->toggle(); return;
            case Qt::Key_C:
                if (! shift)
                {
                    clickButton->toggle();
                    return;
                }
                break;
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
                case Qt::Key_R:
                    // A clip picked: R reverses it (owner, 2026-10-25); else it arms the track.
                    if (showingClip)
                        reverseShownClip();
                    else
                        arm (selected);
                    return;
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
    // In the editor's place it is not a window: the main window tells it has the keyboard.
    if (e->type() == QEvent::ActivationChange && isWindow() && isActiveWindow())
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
    appSettings().setValue (QStringLiteral ("mixer/sideColumn"), sideSplitter->saveState());
    QWidget::closeEvent (e);
    Q_EMIT closed();
}

//==============================================================================
QString MixerWindow::autosavePath() { return configFile ("last.phi"); }

bool MixerWindow::isProjectFile (const QString& file)
{
    const auto suffix = QFileInfo (file).suffix();
    return suffix.compare (projectSuffix, Qt::CaseInsensitive) == 0 || suffix.compare (QLatin1String ("phinyxmix"), Qt::CaseInsensitive) == 0;
}

bool MixerWindow::newMix()
{
    if (modified && ! path.isEmpty()
        && QMessageBox::question (this, tr ("New Project"), tr ("Start a new project? Changes to %1 that were not saved are lost.").arg (mixName())) != QMessageBox::Yes)
        return false;
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
        s.lowCut = s.highCut = false;
        s.lowCutHz = 80.0f;
        s.highCutHz = 12000.0f;
        s.lowCutSlope = s.highCutSlope = 12;
        s.delayMs = 0.0f;
        s.width = 1.0f;
        s.mode = (int) mixer::TrackMode::hybrid;
        s.input = mixer::Strip::inputAuto;
        s.monitor = mixer::Strip::monitorAuto;
        s.inputChannels = 0;
        s.audioLayerDb = s.instrumentLayerDb = 0.0f;
        s.duckDb = 0.0f;
        s.audioDucksInstrument = false;
        s.duckAttackMs = 1.0f;
        s.duckReleaseMs = 150.0f;
        s.duckKey = -1;
        s.duckBelowHz = 0.0f;
        for (int r = 0; r < mixer::numReturns; ++r)
        {
            s.sendDb[(size_t) r] = -100.0f;
            s.sendPre[(size_t) r] = false;
        }
        engine.setInserts (i, {});
        auto& t = info[(size_t) i];
        if (t.editor != nullptr)
            t.editor->close();
        std::tie (t.name, t.colour) = defaultTrack (i);
        givenDefaults[(size_t) i] = { t.name, t.colour };
        t.sound = nullptr;
        ++t.generation;
        t.busy = 0;
        t.tuning = {};
    }
    for (int t = 0; t < mixer::maxTracks; ++t)
        for (int sc = 0; sc < mixer::maxScenes; ++sc)
        {
            engine.setSlot (t, sc, nullptr);
            slots[(size_t) t][(size_t) sc] = nullptr;
        }
    engine.backToTimeline (-1);
    loadingSlots.clear();
    sceneNames = {};
    sceneTunings = {};
    cursorScene = 0;
    setScenes (16);
    engine.launchQuantise = 4.0;
    quantiseBox->setCurrentIndex (quantiseIndex (4.0));
    undoStack.clear();
    redoStack.clear();
    lanes->clearSelection();
    engine.setNumTracks (hooks.selectorSet ? mixer::maxTracks : 2);   // a track for each category of the set
    engine.setLiveTrack (0);
    masterFader->setValue (faderPosition (0.0));
    defaultReturns();
    restoring = false;
    path.clear();
    modified = false;
    if (! opening && hooks.resetProject)
        hooks.resetProject();
    rebuildStrips();
    select (0);
    updateTitle();
    return true;
}

void MixerWindow::newMixFromSet (const QString& setName, const QStringList& names, const QList<QColor>& colours)
{
    if (! newMix())
        return;
    const auto count = std::min ((int) std::min (names.size(), colours.size()), mixer::maxTracks);
    for (int i = 0; i < count; ++i)
    {
        info[(size_t) i].name = names[count - 1 - i];
        info[(size_t) i].colour = colours[count - 1 - i];
    }
    engine.setNumTracks (std::max (count, 1));
    rebuildStrips();
    select (0);
    status->setText (tr ("A new project from the %1 set").arg (setName));
}

void MixerWindow::openMix()
{
    if (modified && QMessageBox::question (this, tr ("Open Project"), tr ("Open another project? Changes to %1 that were not saved are lost.").arg (mixName())) != QMessageBox::Yes)
        return;
    const auto file = QFileDialog::getOpenFileName (this, tr ("Open Project"), folder(), projectFilter());
    if (file.isEmpty())
        return;
    QString error;
    if (! loadMix (file, error))
        QMessageBox::warning (this, tr ("Open Project"), error);
    setFocus();
}

bool MixerWindow::openMixFile (const QString& file)
{
    if (modified && QMessageBox::question (this, tr ("Open Project"), tr ("Open %1? Changes to %2 that were not saved are lost.")
                                                                           .arg (QFileInfo (file).fileName(), mixName())) != QMessageBox::Yes)
        return false;
    QString error;
    if (! loadMix (QFileInfo (file).absoluteFilePath(), error))
    {
        QMessageBox::warning (this, tr ("Open Project"), error);
        return false;
    }
    return true;
}

bool MixerWindow::saveMix (bool saveAs)
{
    auto file = path;
    // A mix from before projects is saved as a project beside it; the old file is left alone.
    if (! saveAs && ! file.isEmpty() && QFileInfo (file).suffix().compare (projectSuffix, Qt::CaseInsensitive) != 0)
        file = QFileInfo (file).absoluteDir().filePath (QFileInfo (file).completeBaseName() + QLatin1Char ('.') + projectSuffix);
    if (saveAs || file.isEmpty())
    {
        file = QFileDialog::getSaveFileName (this, tr ("Save Project"), QDir (folder()).filePath (mixName() + QLatin1Char ('.') + projectSuffix),
                                             tr ("PHI projects (*.phi)"));
        if (file.isEmpty())
            return false;
        if (QFileInfo (file).suffix().compare (projectSuffix, Qt::CaseInsensitive) != 0)
            file += QLatin1Char ('.') + projectSuffix;
    }
    QString error;
    if (! writeMix (file, error))
    {
        QMessageBox::warning (this, tr ("Save Project"), error);
        return false;
    }
    path = file;
    modified = false;
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());
    updateTitle();
    status->setText (tr ("Saved the project to %1").arg (file));
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
            { "lowCut", QJsonObject { { "on", s.lowCut.load() }, { "hz", s.lowCutHz.load() }, { "slope", s.lowCutSlope.load() } } },
            { "highCut", QJsonObject { { "on", s.highCut.load() }, { "hz", s.highCutHz.load() }, { "slope", s.highCutSlope.load() } } },
            { "delayMs", s.delayMs.load() }, { "width", s.width.load() }, { "inserts", insertsJson (i) },
            { "mode", s.mode.load() }, { "audioLayer", s.audioLayerDb.load() }, { "instrumentLayer", s.instrumentLayerDb.load() },
            { "input", s.input.load() }, { "monitor", s.monitor.load() }, { "inputChannels", s.inputChannels.load() },
            { "duck", QJsonObject { { "db", s.duckDb.load() }, { "audioLeads", s.audioDucksInstrument.load() },
                                    { "attack", s.duckAttackMs.load() }, { "release", s.duckReleaseMs.load() },
                                    { "key", s.duckKey.load() }, { "below", s.duckBelowHz.load() } } },
            { "sends", QJsonArray { QJsonObject { { "db", s.sendDb[0].load() }, { "pre", s.sendPre[0].load() } },
                                    QJsonObject { { "db", s.sendDb[1].load() }, { "pre", s.sendPre[1].load() } } } },
            { "tuning", tuningJson (t.tuning) },
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
                case mixer::Instrument::Kind::raga:
                    io = { { "kind", "raga" }, { "state", QString::fromStdString (inst->state()) } };
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
                    io = { { "kind", "slicer" }, { "sound", soundFile (sl.engine().sample(), sl.samplePath) }, { "kit", slicerkit::toJson (sl.engine().kit(), base, [&] (const sampler::Sample& padSound, int pad, int layer)
                         {
                             // A pad's own sound that came from no file goes beside the mix too.
                             const auto folderName = QFileInfo (file).completeBaseName() + tr (" Samples");
                             base.mkpath (folderName);
                             const auto wav = base.filePath (folderName + QStringLiteral ("/Track %1 pad %2 layer %3.wav").arg (i + 1).arg (pad + 1).arg (layer + 1));
                             QString why;
                             if (slicerkit::saveSound (padSound, rate(), wav, why))
                                 return wav;
                             error = tr ("Track %1's pad %2 could not be saved: %3").arg (i + 1).arg (pad + 1).arg (why);
                             return QString();
                         }) } };
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
    // The launcher's slots, in seconds like the clips.
    QJsonArray slotList;
    for (int t = 0; t < engine.numTracks(); ++t)
        for (int sc = 0; sc < mixer::maxScenes; ++sc)
            if (const auto& slot = slots[(size_t) t][(size_t) sc]; slot != nullptr)
            {
                const auto& c = slot->clip;
                const auto source = c.audio != nullptr ? c.audio->path : c.midi->path;
                QJsonObject o {
                    { "track", t }, { "scene", sc }, { "kind", c.audio != nullptr ? "audio" : "midi" }, { "file", storedPath (QString::fromStdString (source), base) },
                    { "offset", (double) c.offset / rate() }, { "length", (double) c.length / rate() }, { "reversed", c.reversed },
                    { "loop", slot->loop }, { "quantise", slot->quantise }, { "tuning", tuningJson (slot->tuning) },
                };
                if (c.audio != nullptr && c.midi != nullptr && ! c.midi->path.empty())
                    o.insert ("midiLayer", storedPath (QString::fromStdString (c.midi->path), base));   // both layers
                slotList.append (o);
            }
    QJsonArray names, tunings;
    for (const auto& name : sceneNames)
        names.append (name);
    for (const auto& tuning : sceneTunings)
        tunings.append (tuningJson (tuning));
    const QJsonObject launcher {
        { "scenes", numScenes }, { "quantise", engine.launchQuantise.load() }, { "sceneNames", names }, { "sceneTunings", tunings },
        { "slots", slotList }, { "shown", launcherShown() },
    };
    // The phinyx keys are from before the rename to PHI; kept so every version reads every project.
    QJsonObject root {
        { "phinyxProject", 1 }, { "phinyxMix", 3 }, { "tempo", tempoBox->value() }, { "beatsPerBar", beatsBox->value() }, { "midiFollowsTempo", followTempoBox->isChecked() },
        { "master", engine.masterDb.load() }, { "armed", engine.liveTrack() }, { "tracks", tracks }, { "launcher", launcher },
        { "returns", returnsJson() },
    };
    if (hooks.projectState)
    {
        const auto extra = hooks.projectState();
        for (auto it = extra.begin(); it != extra.end(); ++it)
            root.insert (it.key(), it.value());
    }
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
        error = tr ("%1 is not a PHI project.").arg (QFileInfo (file).fileName());
        return false;
    }
    const auto keepPath = path;
    path.clear();
    modified = false;
    opening = true;
    newMix();
    opening = false;
    restoring = true;
    const QDir base = QFileInfo (file).absoluteDir();
    QStringList problems;
    {
        const QSignalBlocker a (tempoBox), b (followTempoBox);
        tempoBox->setValue (root.value ("tempo").toDouble (120.0));
        beatsBox->setValue (std::clamp (root.value ("beatsPerBar").toInt (4), 1, 12));
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
        t.tuning = tuningFromJson (o.value ("tuning"));
        s.transpose = o.value ("transpose").toInt();
        s.channels = (uint32_t) o.value ("channels").toInt (0xFFFF);
        s.trimDb = (float) o.value ("trim").toDouble();
        s.faderDb = (float) o.value ("fader").toDouble();
        s.pan = (float) o.value ("pan").toDouble();
        s.invertLeft = o.value ("invertLeft").toBool();
        s.invertRight = o.value ("invertRight").toBool();
        s.mute = o.value ("mute").toBool();
        s.solo = o.value ("solo").toBool();
        const auto lowCut = o.value ("lowCut").toObject(), highCut = o.value ("highCut").toObject();
        s.lowCut = lowCut.value ("on").toBool();
        s.lowCutHz = (float) lowCut.value ("hz").toDouble (80.0);
        s.lowCutSlope = lowCut.value ("slope").toInt (12);
        s.highCut = highCut.value ("on").toBool();
        s.highCutHz = (float) highCut.value ("hz").toDouble (12000.0);
        s.highCutSlope = highCut.value ("slope").toInt (12);
        s.delayMs = (float) std::clamp (o.value ("delayMs").toDouble(), (double) -mixer::maxDelayMs, (double) mixer::maxDelayMs);
        s.width = (float) std::clamp (o.value ("width").toDouble (1.0), 0.0, 2.0);
        loadInserts (i, o.value ("inserts").toArray(), problems);
        s.mode = std::clamp (o.value ("mode").toInt(), 0, 2);
        s.input = std::clamp (o.value ("input").toInt(), 0, (int) mixer::Strip::inputInstrument);
        s.monitor = std::clamp (o.value ("monitor").toInt(), 0, 2);
        s.inputChannels = std::clamp (o.value ("inputChannels").toInt(), 0, 2);
        s.audioLayerDb = (float) o.value ("audioLayer").toDouble();
        s.instrumentLayerDb = (float) o.value ("instrumentLayer").toDouble();
        const auto duck = o.value ("duck").toObject();
        s.duckDb = (float) std::clamp (duck.value ("db").toDouble(), 0.0, 48.0);
        s.audioDucksInstrument = duck.value ("audioLeads").toBool();
        s.duckAttackMs = (float) duck.value ("attack").toDouble (1.0);
        s.duckReleaseMs = (float) duck.value ("release").toDouble (150.0);
        s.duckKey = std::clamp (duck.value ("key").toInt (-1), -1, mixer::maxTracks - 1);
        s.duckBelowHz = (float) std::clamp (duck.value ("below").toDouble(), 0.0, 2000.0);
        const auto sends = o.value ("sends").toArray();
        for (int r = 0; r < mixer::numReturns && r < sends.size(); ++r)
        {
            const auto send = sends[r].toObject();
            s.sendDb[(size_t) r] = (float) std::clamp (send.value ("db").toDouble (-100.0), -100.0, 0.0);
            s.sendPre[(size_t) r] = send.value ("pre").toBool();
        }

        const auto io = o.value ("instrument").toObject();
        const auto kind = io.value ("kind").toString();
        if (kind == QLatin1String ("synth") || kind == QLatin1String ("raga"))
        {
            setInstrumentKind (i, kind == QLatin1String ("raga") ? ragaChoice : synthChoice, {});
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
                QStringList missing;
                sl.engine().setKit (slicerkit::fromJson (io.value ("kit").toObject(), base, rate(), &missing));
                for (const auto& m : missing)
                    problems << tr ("Track %1: %2").arg (i + 1).arg (m);
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
    engine.tempo = tempoBox->value();
    if (root.contains (QStringLiteral ("returns")))
        loadReturns (root.value ("returns").toArray(), problems);

    // The launcher (version 3 on).
    const auto launcher = root.value ("launcher").toObject();
    if (! launcher.isEmpty())
    {
        setScenes (launcher.value ("scenes").toInt (16));
        engine.launchQuantise = std::max (0.0, launcher.value ("quantise").toDouble (4.0));
        quantiseBox->setCurrentIndex (quantiseIndex (engine.launchQuantise.load()));
        const auto names = launcher.value ("sceneNames").toArray();
        for (int sc = 0; sc < std::min ((int) names.size(), mixer::maxScenes); ++sc)
            sceneNames[(size_t) sc] = names[sc].toString();
        const auto tunings = launcher.value ("sceneTunings").toArray();
        for (int sc = 0; sc < std::min ((int) tunings.size(), mixer::maxScenes); ++sc)
            sceneTunings[(size_t) sc] = tuningFromJson (tunings[sc]);
        const auto r = rate();
        for (const auto& value : launcher.value ("slots").toArray())
        {
            const auto o = value.toObject();
            const auto t = o.value ("track").toInt (-1), sc = o.value ("scene").toInt (-1);
            if (t < 0 || t >= engine.numTracks() || sc < 0 || sc >= mixer::maxScenes)
                continue;
            const auto source = resolvedPath (o.value ("file").toString(), base);
            if (! QFileInfo::exists (source))
            {
                problems << tr ("%1, %2: %3 is missing").arg (trackName (t), sceneName (sc), source);
                continue;
            }
            mixer::Slot like;
            like.loop = o.value ("loop").toBool (true);
            like.quantise = o.value ("quantise").toDouble (-1.0);
            like.tuning = tuningFromJson (o.value ("tuning"));
            like.clip.offset = (Frame) std::llround (o.value ("offset").toDouble() * r);
            like.clip.length = (Frame) std::llround (o.value ("length").toDouble() * r);
            like.clip.reversed = o.value ("reversed").toBool();
            const auto layer = o.contains ("midiLayer") ? resolvedPath (o.value ("midiLayer").toString(), base) : QString();
            loadSlot (t, sc, source, like, layer);
        }
    }
    engine.masterDb = (float) root.value ("master").toDouble();
    masterFader->setValue (faderPosition (engine.masterDb.load()));
    engine.setLiveTrack (root.value ("armed").toInt (0));
    if (hooks.setProjectState)
        hooks.setProjectState (root);
    restoring = false;
    path = file;
    modified = false;
    (void) keepPath;
    rebuildStrips();
    select (0);
    if (launcher.contains (QStringLiteral ("shown")))
        showLauncher (launcher.value ("shown").toBool());
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
    appSettings().setValue (QStringLiteral ("mixer/lastModified"), modified);
}

void MixerWindow::restoreAutosave()
{
    auto file = autosavePath();
    if (! QFileInfo::exists (file))
        file = configFile ("last.phinyxmix");   // kept by versions before projects
    if (! QFileInfo::exists (file))
        return;
    QString error;
    const auto lastPath = appSettings().value (QStringLiteral ("mixer/lastPath")).toString();
    if (loadMix (file, error))
    {
        // It is the project that was open, not a new one called "last", with the
        // changes not yet saved to it still to save.
        path = lastPath;
        modified = appSettings().value (QStringLiteral ("mixer/lastModified"), false).toBool();
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
    const auto base = path.isEmpty() ? QDir (QStandardPaths::writableLocation (QStandardPaths::MusicLocation)).filePath (tr ("PHI Bounces"))
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
        // So are the cuts, the inserts (bypassed, kept), the width and the delay.
        from.trimDb = 0.0f;
        from.invertLeft = from.invertRight = false;
        from.lowCut = from.highCut = false;
        from.width = 1.0f;
        from.delayMs = 0.0f;
        auto inserts = engine.inserts (track);
        for (auto& x : inserts)
            x.bypassed = true;
        engine.setInserts (track, std::move (inserts));
    }
    else
    {
        target = engine.numTracks();
        engine.setNumTracks (target + 1);
        auto& to = engine.strip (target);
        to.trimDb = 0.0f;
        to.invertLeft = to.invertRight = false;
        to.lowCut = to.highCut = false;
        to.width = 1.0f;
        to.delayMs = 0.0f;
        engine.setInserts (target, {});
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
    // From the launcher, V goes back to the tracks across time.
    const bool show = launcherShown() || lanes->isHidden();
    if (launcherShown())
        showLauncher (false);
    lanes->setVisible (show);
    updateUpperArea();
    appSettings().setValue (QStringLiteral ("mixer/lanes"), show);
    status->setText (show ? tr ("The tracks across time are shown above the strips (V hides them).")
                          : tr ("The tracks across time are hidden (V shows them)."));
}

void MixerWindow::updateUpperArea()
{
    upperArea->setVisible (! lanes->isHidden() || ! launcherArea->isHidden());
}

void MixerWindow::toggleBrowser()
{
    showSideFinder (! finderShown);
    if (! finderShown)
        setFocus();
}

void MixerWindow::showSideFinder (bool finder)
{
    finderShown = finder;
    if (inspectorButton != nullptr)
    {
        const QSignalBlocker quiet (inspectorButton);
        inspectorButton->setChecked (! finder);
    }
    if (! isWindow())
    {
        // In the editor's place: the editor's Find panel is the Finder, beside the mixer.
        sideStack->setCurrentWidget (inspectorScroll);
        sideStack->setVisible (! finder);
        Q_EMIT finderWanted (finder);
    }
    else
    {
        sideStack->show();
        sideStack->setCurrentWidget (finder ? static_cast<QWidget*> (browser) : inspectorScroll);
        if (finder && isVisible())
            browser->view()->setFocus();
    }
    if (! finder)
        refreshInspector();
}

void MixerWindow::engageTrack()
{
    if (finderShown)
        showSideFinder (false);
}

QList<QWidget*> MixerWindow::focusAreas() const
{
    return { lanes, browser->view(), const_cast<MixerWindow*> (this) };
}

void MixerWindow::toggleDevicePanel()
{
    if (showingClip)
    {
        showDevices();
        status->setText (tr ("The Device Chain (Shift+Tab over it goes back to the clip)"));
    }
    else if (clipView->shownTrack() == selected && clipView->reload())
    {
        showingClip = true;
        devicePanel->setCurrentWidget (clipView);
        updateDevicePanel();
        status->setText (tr ("The clip (Shift+Tab over it goes back to the Device Chain)"));
    }
    else
        status->setText (tr ("Click a clip of this track to show it here"));
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

// Tab (and Shift+Tab) switch between the clip launcher and the tracks across time, as in
// Ableton (owner, 2026-10-25); F6 still goes round the sections.
bool MixerWindow::focusNextPrevChild (bool next)
{
    // Shift+Tab with the pointer over the bottom panel: the Device Chain or the clip.
    if (! next && devicePanel->underMouse())
    {
        toggleDevicePanel();
        return true;
    }
    if (launcherShown())
    {
        showLauncher (false);
        lanes->show();
        appSettings().setValue (QStringLiteral ("mixer/lanes"), true);
        updateUpperArea();
    }
    else
        showLauncher (true);
    setFocus();
    return true;
}

//==============================================================================
// A clip edited in the editor: what it sounds like now, and the edit put back.
namespace
{
    // The part of an audio clip heard, the way it plays.
    AudioDataPtr heardAudio (const mixer::Clip& c, double rate)
    {
        const auto& a = *c.audio;
        std::vector<float> left ((size_t) c.length), right ((size_t) c.length);
        for (Frame k = 0; k < c.length; ++k)
        {
            const auto forward = c.offset + k;
            const auto src = (size_t) (c.reversed ? a.length() - 1 - forward : forward);
            left[(size_t) k] = a.left[src];
            right[(size_t) k] = a.right[src];
        }
        AudioData::Builder b (a.mono ? 1 : 2, rate);
        const float* ptrs[2] = { left.data(), right.data() };
        b.append (ptrs, (int) c.length);
        return b.finish();
    }
}

AudioDataPtr MixerWindow::clipAudio (int track, uint32_t id, QString& name) const
{
    if (track < 0 || track >= engine.numTracks())
        return nullptr;
    for (const auto& c : engine.clips (track))
    {
        if (c.id != id || c.audio == nullptr)
            continue;
        name = tr ("Track %1 %2").arg (track + 1).arg (QFileInfo (QString::fromStdString (c.audio->path)).completeBaseName());
        return heardAudio (c, rate());
    }
    return nullptr;
}

mixer::AudioClipPtr MixerWindow::saveClipEdit (const AudioData& edited, const QString& name, QString& file, QString& error)
{
    // Saved beside the mix (in the Music folder until it is saved), the original left as it was.
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = name;
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    file = QDir (base).filePath (stem + QStringLiteral (".wav"));
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
        return nullptr;
    }
    const auto clip = mixer::makeAudioClip (*sound, file.toStdString());
    audioCache[file] = clip;
    return clip;
}

bool MixerWindow::sendAudioToLauncher (const AudioData& sound, const QString& name, QString& error)
{
    QString file;
    if (saveClipEdit (sound, name, file, error) == nullptr)
        return false;
    // Under the launcher's cursor when that slot is free, else the track's first empty one.
    const auto track = selected < mixer::masterIndex ? selected : 0;
    auto scene = slots[(size_t) track][(size_t) cursorScene] == nullptr ? cursorScene : firstEmptyScene (track);
    if (scene < 0)
    {
        if (numScenes >= mixer::maxScenes)
        {
            error = tr ("%1 is full: all %2 scenes have a clip. Empty a slot (Delete) first.").arg (trackName (track)).arg (mixer::maxScenes);
            return false;
        }
        scene = numScenes;   // a scene more
    }
    if (! launcherShown())
        showLauncher (true);
    loadSlot (track, scene, file);
    moveCursor (track, scene);
    status->setText (tr ("%1 is in the launcher on %2 (%3)").arg (name, trackName (track), file));
    return true;
}

void MixerWindow::drumsToMidi (int track, uint32_t id, int scene)
{
    if (track < 0 || track >= engine.numTracks())
        return;
    mixer::Clip clip;
    mixer::SlotPtr slot;
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && scene < mixer::maxScenes && (slot = slots[(size_t) track][(size_t) scene]) != nullptr)
        clip = slot->clip;
    if (clip.audio == nullptr || clip.length <= 0)
        return;
    if (slot != nullptr && clip.midi != nullptr)
    {
        say (tr ("%1 has its MIDI layer already.").arg (sceneName (scene)));
        return;
    }
    const auto midi = mixer::drumsToMidi (clip, rate(), engine.tempo.load());
    if (midi->events.empty())
    {
        say (tr ("No drum hits were found in the clip."));
        return;
    }
    mixer::Recorded rec;
    rec.track = track;
    rec.scene = scene;
    rec.midi = midi;
    rec.length = clip.length;
    rec.notes = true;
    QString error;
    const auto file = saveTake (rec, error);
    if (file.isEmpty())
    {
        say (error);
        return;
    }
    if (slot != nullptr)
    {
        // The notes are the clip as it plays, from its first frame: so is its audio from now on.
        editSlot (track, scene, [&] (mixer::Slot& x)
        {
            if (x.clip.offset != 0 || x.clip.reversed)
                x.clip.audio = layers::flatAudio (x.clip);
            x.clip.offset = 0;
            x.clip.reversed = false;
            x.clip.midi = midi;
        });
    }
    else
    {
        remember();
        auto list = engine.clips (track);
        auto notes = mixer::makeClip (mixer::MidiClipPtr (midi), clip.start);
        notes.length = clip.length;
        notes.id = nextClipId++;
        list.push_back (notes);
        editClips (track, std::move (list));
    }
    // The Synth plays the General MIDI kit on channel 10; another instrument is the track's choice.
    const bool hadInstrument = engine.instrument (track) != nullptr;
    giveSynth (track);
    giveBothLayers (track);
    changed();
    refreshViews();
    say (hadInstrument ? tr ("Drums to MIDI: %1 (on channel 10, for the track's instrument to play)").arg (QFileInfo (file).fileName())
                       : tr ("Drums to MIDI: %1, played by the Synth's drum kit; choose another instrument for the track to change it").arg (QFileInfo (file).fileName()));
}

void MixerWindow::stretchShownAudio()
{
    if (! showingClip || clipView->showsMidi())
        return;
    const auto track = clipView->shownTrack();
    const auto id = clipView->shownId();
    const auto scene = clipView->shownScene();
    mixer::Clip clip;
    if (id != 0)
    {
        for (const auto& c : engine.clips (track))
            if (c.id == id)
                clip = c;
    }
    else if (scene >= 0 && slots[(size_t) track][(size_t) scene] != nullptr)
        clip = slots[(size_t) track][(size_t) scene]->clip;
    if (clip.audio == nullptr || clip.length < 2)
        return;
    const auto ratio = stretchRatioBox->value();
    appSettings().setValue (QStringLiteral ("extreme/ratio"), ratio);
    const auto sound = heardAudio (clip, rate());
    const auto settings = loadExtremeSettings();
    const auto title = tr ("Paulstretch");

    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    QProgressDialog dialog (tr ("Stretching %1 %2 times...").arg (clipView->shownName()).arg (ratio), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (title);
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
    watcher.setFuture (QtConcurrent::run ([sound, ratio, settings, progress, cancel]
    {
        Result r;
        r.data = paulstretch::stretch (*sound, 0, sound->length(), ratio, settings, r.error, progress.get(), cancel.get());
        return r;
    }));
    loop.exec();
    poll.stop();
    dialog.reset();

    const auto result = watcher.result();
    if (result.data == nullptr)
    {
        if (! cancel->load())
            QMessageBox::warning (this, title, result.error.empty() ? tr ("The clip could not be stretched.") : QString::fromStdString (result.error));
        return;
    }
    auto stem = QFileInfo (QString::fromStdString (clip.audio->path)).completeBaseName();
    if (stem.isEmpty())
        stem = tr ("Clip");
    stem.remove (QRegularExpression (QStringLiteral (" x[\\d.]+( \\d+)?$")));   // "pad x8" stretched again is "pad x4", not "pad x8 x4"
    QString file, error;
    const auto stretched = saveClipEdit (*result.data, QStringLiteral ("%1 x%2").arg (stem).arg (ratio), file, error);
    if (stretched == nullptr)
    {
        QMessageBox::warning (this, title, error);
        return;
    }
    if (id != 0)
    {
        remember();
        auto list = engine.clips (track);
        for (auto& c : list)
            if (c.id == id)
            {
                c.audio = stretched;
                c.offset = 0;
                c.length = stretched->length();
                c.reversed = false;
            }
        editClips (track, std::move (list));
        status->setText (tr ("Stretched %1 times (%2); Ctrl+Z puts the clip back").arg (ratio).arg (file));
    }
    else
    {
        editSlot (track, scene, [&stretched] (mixer::Slot& x)
        {
            x.clip.audio = stretched;
            x.clip.offset = 0;
            x.clip.length = stretched->length();
            x.clip.reversed = false;
        });
        status->setText (tr ("Stretched %1 times (%2)").arg (ratio).arg (file));
    }
    changed();
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

    QString file;
    const auto clip = saveClipEdit (edited, name, file, error);
    if (clip == nullptr)
        return false;

    remember();
    it->audio = clip;
    it->offset = 0;
    it->length = clip->length();
    it->reversed = false;   // the edit is the clip as it played
    editClips (track, std::move (list));
    status->setText (tr ("The edit is on track %1 (%2); Ctrl+Z puts the clip back as it was").arg (track + 1).arg (file));
    return true;
}

//==============================================================================
// The clip launcher.
void MixerWindow::showLauncher (bool on)
{
    launcherArea->setVisible (on);
    if (on)
        lanes->hide();
    else
        lanes->setVisible (appSettings().value (QStringLiteral ("mixer/lanes"), true).toBool());
    updateUpperArea();
    // The strips stay lean in both views; the Inspector has the rest.
    for (auto* strip : strips)
        strip->setCompact (true);
    // The mixer keeps its height in both views (owner, 2026-10-26); the scenes scroll above it,
    // their clips tall enough to show both layers.
    for (auto* w : std::initializer_list<QWidget*> { quantiseBox, scenesBox, (QWidget*) rack })
        w->setVisible (on);
    {
        const QSignalBlocker block (launcherButton);
        launcherButton->setChecked (on);
    }
    rack->refresh();
    grid->update();
}

bool MixerWindow::launcherShown() const { return launcherArea != nullptr && ! launcherArea->isHidden(); }

QString MixerWindow::sceneName (int scene) const
{
    if (scene < 0 || scene >= mixer::maxScenes)
        return {};
    return sceneNames[(size_t) scene].isEmpty() ? tr ("Scene %1").arg (scene + 1) : sceneNames[(size_t) scene];
}

QString MixerWindow::slotName (int track, int scene) const
{
    const auto& slot = slots[(size_t) track][(size_t) scene];
    if (slot == nullptr)
        return {};
    const auto& c = slot->clip;
    return QFileInfo (QString::fromStdString (c.audio != nullptr ? c.audio->path : c.midi->path)).completeBaseName();
}

int MixerWindow::firstEmptyScene (int track) const
{
    for (int s = 0; s < numScenes; ++s)
        if (slots[(size_t) track][(size_t) s] == nullptr && loadingSlots.count ({ track, s }) == 0)
            return s;
    return -1;
}

void MixerWindow::setScenes (int count)
{
    numScenes = std::clamp (count, 1, mixer::maxScenes);
    cursorScene = std::min (cursorScene, numScenes - 1);
    {
        const QSignalBlocker block (scenesBox);
        scenesBox->setValue (numScenes);
    }
    grid->updateMinimum();
    grid->update();
    rack->refresh();
}

void MixerWindow::moveCursor (int track, int scene)
{
    if (track >= 0 && track < engine.numTracks() && track != selected)
        select (track);
    cursorScene = std::clamp (scene, 0, numScenes - 1);
    const auto r = grid->sceneArea (cursorScene);
    gridScroll->ensureVisible (r.center().x(), r.center().y(), 0, r.height());
    grid->update();
    rack->refresh();
}

void MixerWindow::launchSlot (int track, int scene)
{
    if (track < 0 || track >= engine.numTracks())
        return;
    const bool wasPlaying = engine.isPlaying();
    if (slots[(size_t) track][(size_t) scene] != nullptr)
        engine.launch (track, scene);
    else
        engine.stopTrack (track);
    if (! wasPlaying && engine.isPlaying())
        Q_EMIT playStarted();
    grid->update();
}

void MixerWindow::renameScene (int scene)
{
    bool ok = false;
    const auto name = QInputDialog::getText (this, tr ("Rename Scene"), tr ("Name (empty for \"Scene %1\"):").arg (scene + 1), QLineEdit::Normal,
                                             sceneNames[(size_t) scene], &ok);
    if (ok)
    {
        sceneNames[(size_t) scene] = name.trimmed();
        grid->update();
        rack->refresh();
        changed();
    }
    setFocus();
}

mixer::MidiClipPtr MixerWindow::launcherMidi (midifile::SongPtr song, const std::string& file) const
{
    const auto bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    auto placed = mixer::placeMidi (song, file, rate(), bpm);
    if (placed == nullptr)
        return nullptr;
    // A loop goes round in whole bars: its length is rounded up to the bar at the tempo it plays at.
    const auto playBpm = bpm > 0.0 ? bpm : song->fileBpm() > 0.0 ? song->fileBpm() : tempoBox->value();
    const auto bar = 4.0 * 60.0 / playBpm * rate();
    const auto bars = std::max (1.0, std::ceil ((double) placed->length / bar - 0.01));
    auto whole = std::make_shared<mixer::MidiClip> (*placed);
    whole->length = std::max (placed->length, (Frame) std::llround (bars * bar));
    return whole;
}

void MixerWindow::loadSlot (int track, int scene, const QString& file, mixer::Slot like, const QString& midiLayer)
{
    if (track < 0 || track >= engine.numTracks() || scene < 0 || scene >= mixer::maxScenes)
        return;
    if (scene >= numScenes)
        setScenes (scene + 1);
    const bool fromFile = restoring;   // a project being opened: not a change to it
    auto place = [this, track, scene, like, fromFile, midiLayer, name = QFileInfo (file).fileName()] (mixer::Clip clip)
    {
        loadingSlots.erase ({ track, scene });
        // A MIDI layer over the audio: its notes from the start of the audio, as recorded.
        if (! midiLayer.isEmpty() && clip.audio != nullptr)
        {
            std::string why;
            if (auto song = midifile::read (midiLayer.toStdString(), why))
            {
                auto midi = launcherMidi (song, midiLayer.toStdString());
                clip.midi = like.clip.reversed ? mixer::reverseMidi (*midi) : midi;
            }
            else
                status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (midiLayer).fileName(), QString::fromStdString (why)));
        }
        auto slot = std::make_shared<mixer::Slot> (like);
        const auto source = clip.sourceLength();
        clip.offset = std::clamp<Frame> (like.clip.offset, 0, std::max<Frame> (0, source - 1));
        clip.length = like.clip.length > 0 ? std::min (like.clip.length, source - clip.offset) : source - clip.offset;
        clip.reversed = like.clip.reversed;
        slot->clip = clip;
        slots[(size_t) track][(size_t) scene] = slot;
        engine.setSlot (track, scene, slot);
        if (! fromFile)
            changed();
        grid->update();
        rack->refresh();
        status->setText (tr ("%1 is in %2, %3").arg (name, trackName (track), sceneName (scene)));
    };
    if (isMidiFile (file))
    {
        std::string why;
        auto song = midifile::read (file.toStdString(), why);
        if (song == nullptr)
        {
            status->setText (tr ("%1 could not be read: %2").arg (QFileInfo (file).fileName(), QString::fromStdString (why)));
            return;
        }
        appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());
        auto midi = launcherMidi (song, file.toStdString());
        if (like.clip.reversed)
            midi = mixer::reverseMidi (*midi);
        place (mixer::makeClip (midi, 0));
        // A MIDI file needs something to play it: the Synth until another instrument is chosen.
        if (engine.instrument (track) == nullptr && ! restoring)
        {
            setInstrumentKind (track, synthChoice, {});
            strips[(size_t) track]->refresh();
            rack->refresh();
        }
        return;
    }
    loadingSlots.insert ({ track, scene });
    grid->update();
    QPointer<MixerWindow> self (this);
    decodeAudio (track, file, [self, track, scene, place] (mixer::AudioClipPtr sound)
    {
        if (self == nullptr)
            return;
        if (sound == nullptr)
        {
            self->loadingSlots.erase ({ track, scene });
            self->grid->update();
            return;
        }
        place (mixer::makeClip (sound, 0));
    });
}

bool MixerWindow::addLayerFile (int track, int scene, const QString& file)
{
    const auto slot = track >= 0 && track < engine.numTracks() && scene >= 0 && scene < mixer::maxScenes ? slots[(size_t) track][(size_t) scene] : nullptr;
    if (slot == nullptr || slot->clip.length <= 0 || (slot->clip.audio != nullptr) == (slot->clip.midi != nullptr))
        return false;   // empty, or both layers already: the file takes the slot
    const bool midiFile = isMidiFile (file);
    if (midiFile == (slot->clip.midi != nullptr))
        return false;   // the same kind: it takes the slot
    // The slot as it plays, from its first frame, with the new layer from there.
    auto layered = [slot] (mixer::Clip& c)
    {
        if (c.audio != nullptr && (c.offset != 0 || c.reversed))
            c.audio = layers::flatAudio (slot->clip);
        c.offset = 0;
        c.reversed = false;
    };
    const auto name = QFileInfo (file).fileName();
    if (midiFile)
    {
        std::string why;
        auto song = midifile::read (file.toStdString(), why);
        if (song == nullptr)
        {
            say (tr ("%1 could not be read: %2").arg (name, QString::fromStdString (why)));
            return true;
        }
        auto x = std::make_shared<mixer::Slot> (*slot);
        layered (x->clip);
        x->clip.midi = launcherMidi (song, file.toStdString());
        slots[(size_t) track][(size_t) scene] = x;
        engine.setSlot (track, scene, x);
        giveSynth (track);
        giveBothLayers (track);
        changed();
        refreshViews();
        say (tr ("%1 is the MIDI layer over %2, %3").arg (name, trackName (track), sceneName (scene)));
        return true;
    }
    loadingSlots.insert ({ track, scene });
    QPointer<MixerWindow> self (this);
    decodeAudio (track, file, [self, track, scene, slot, layered, name] (mixer::AudioClipPtr sound)
    {
        if (self == nullptr)
            return;
        self->loadingSlots.erase ({ track, scene });
        // Only if the slot is still the one it was dropped on.
        if (sound == nullptr || self->slots[(size_t) track][(size_t) scene] != slot)
        {
            self->refreshViews();
            return;
        }
        auto x = std::make_shared<mixer::Slot> (*slot);
        layered (x->clip);
        // As long as the loop: silence after a shorter sound.
        if (sound->length() < x->clip.length)
        {
            auto padded = std::make_shared<mixer::AudioClip> (*sound);
            padded->left.resize ((size_t) x->clip.length, 0.0f);
            padded->right.resize ((size_t) x->clip.length, 0.0f);
            padded->path.clear();
            mixer::Recorded rec;
            rec.track = track;
            rec.scene = scene;
            rec.audio = padded;
            rec.length = x->clip.length;
            QString error;
            if (self->saveTake (rec, error).isEmpty())
                self->say (error);
            sound = padded;
        }
        x->clip.audio = sound;
        self->slots[(size_t) track][(size_t) scene] = x;
        self->engine.setSlot (track, scene, x);
        self->giveBothLayers (track);
        self->changed();
        self->refreshViews();
        self->say (tr ("%1 is the audio layer under %2, %3").arg (name, self->trackName (track), self->sceneName (scene)));
    });
    refreshViews();
    return true;
}

void MixerWindow::editSlot (int track, int scene, const std::function<void (mixer::Slot&)>& edit)
{
    const auto& current = slots[(size_t) track][(size_t) scene];
    if (current == nullptr)
        return;
    auto slot = std::make_shared<mixer::Slot> (*current);
    edit (*slot);
    slots[(size_t) track][(size_t) scene] = slot;
    engine.setSlot (track, scene, slot);
    changed();
    grid->update();
    rack->refresh();
}

mixer::TuningChoice MixerWindow::tuningInForce (int track) const
{
    const auto playing = engine.launchInfo (track).playing;
    if (playing < 0 || playing >= mixer::maxScenes)
        return info[(size_t) track].tuning;
    const auto& slot = slots[(size_t) track][(size_t) playing];
    return mixer::tuningInForce (slot != nullptr ? slot->tuning : mixer::TuningChoice {}, sceneTunings[(size_t) playing], info[(size_t) track].tuning);
}

QString MixerWindow::tuningText (const mixer::TuningChoice& c) const
{
    if (c.system < 0)
        return tr ("12-TET");
    const auto& system = mixer::tuningSystems()[(size_t) c.system];
    return tr ("%1 on %2").arg (QString::fromStdString (system.name), QString::fromUtf8 (rootNames[c.root]));
}

void MixerWindow::applyTunings()
{
    for (int t = 0; t < engine.numTracks(); ++t)
    {
        const auto inst = engine.instrument (t);
        const auto choice = tuningInForce (t);
        auto& heard = heardTunings[(size_t) t];
        if (choice == heard.choice && inst == heard.instrument.lock())
            continue;
        heard = { choice, inst };
        if (inst == nullptr)
            continue;
        if (choice.system < 0)
            inst->setTuning (nullptr);
        else
            inst->setTuning (std::make_shared<const phi::Tuning> (mixer::makeTuning (mixer::tuningSystems()[(size_t) choice.system], choice.root)));
        // A raga in force: PHI rāga plays it, by its sheet.
        int raga = -1;
        bool equal = false;
        if (choice.system >= 0)
        {
            const auto& system = mixer::tuningSystems()[(size_t) choice.system];
            if (system.family.rfind ("raga", 0) == 0)
            {
                equal = system.family != "raga";
                const auto plain = equal ? system.plain.substr (0, system.plain.size() - std::string (" (equal tempered)").size()) : system.plain;
                const auto& sheets = phi::builtInRagas();
                for (size_t i = 0; i < sheets.size(); ++i)
                    if (sheets[i].plain == plain)
                        raga = (int) i;
            }
        }
        inst->setRaga (raga, choice.root, equal);
    }
    // MTS-ESP: the live track's tuning (else the selected track's), for the synths following it.
    if (mtsEsp != nullptr && mtsEsp->running())
    {
        const auto track = engine.liveTrack() >= 0 && engine.liveTrack() < engine.numTracks() ? engine.liveTrack()
                         : selected < engine.numTracks() ? selected : -1;
        const auto choice = track >= 0 ? tuningInForce (track) : mixer::TuningChoice {};
        const auto tuning = choice.system >= 0 ? mixer::makeTuning (mixer::tuningSystems()[(size_t) choice.system], choice.root) : phi::Tuning {};
        mtsEsp->send (tuning, tuningText (choice).toStdString());
    }
}

void MixerWindow::installLibMts()
{
    // libMTS is ODDSound's, free but not open source, so PHI does not include it: it is fetched
    // from ODDSound's own repository when asked, and installed with the user's password.
    const auto url = QString::fromStdString (mixer::MtsEsp::downloadUrl());
    const auto manual = tr ("Or, in a terminal:\n\ncurl -L -o libMTS.so %1\nsudo install -m 755 libMTS.so %2")
                            .arg (url.isEmpty() ? tr ("(ODDSound's libMTS for your processor, from github.com/ODDSound/MTS-ESP)") : url,
                                  QString::fromLatin1 (mixer::MtsEsp::libraryPath));
    QMessageBox ask (QMessageBox::Question, tr ("MTS-ESP"),
                     tr ("MTS-ESP needs ODDSound's libMTS library, which is free but not part of PHI. Install it now? "
                         "PHI downloads it from ODDSound's repository on GitHub and puts it in %1, which asks for your password.")
                         .arg (QString::fromLatin1 (mixer::MtsEsp::libraryPath)),
                     QMessageBox::Cancel, this);
    ask.setDetailedText (manual);
    auto* install = ask.addButton (tr ("Install libMTS"), QMessageBox::AcceptRole);
    install->setEnabled (! url.isEmpty());
    ask.exec();
    if (ask.clickedButton() != install)
        return;
    const auto file = QDir::temp().filePath (QStringLiteral ("phi-libMTS.so"));
    QFile::remove (file);
    QApplication::setOverrideCursor (Qt::WaitCursor);
    QProcess curl;
    curl.start (QStringLiteral ("curl"), { QStringLiteral ("-fsSL"), QStringLiteral ("-o"), file, url });
    const auto fetched = curl.waitForFinished (120000) && curl.exitStatus() == QProcess::NormalExit && curl.exitCode() == 0;
    QApplication::restoreOverrideCursor();
    // An ELF library, not an error page.
    QFile got (file);
    const auto elf = fetched && got.open (QIODevice::ReadOnly) && got.read (4) == QByteArray ("\x7f" "ELF", 4);
    got.close();
    if (! elf)
    {
        QFile::remove (file);
        QMessageBox::warning (this, tr ("MTS-ESP"), tr ("libMTS could not be downloaded (is curl installed, and are you online?).") + QStringLiteral ("\n\n") + manual);
        return;
    }
    // pkexec asks for the password in a window of its own.
    QProcess put;
    put.start (QStringLiteral ("pkexec"), { QStringLiteral ("install"), QStringLiteral ("-m"), QStringLiteral ("755"), file,
                                            QString::fromLatin1 (mixer::MtsEsp::libraryPath) });
    const auto installed = put.waitForFinished (300000) && put.exitStatus() == QProcess::NormalExit && put.exitCode() == 0;
    QFile::remove (file);
    if (! installed || ! mixer::MtsEsp::installed())
    {
        QMessageBox::warning (this, tr ("MTS-ESP"), tr ("libMTS was downloaded but could not be put in place.") + QStringLiteral ("\n\n") + manual);
        return;
    }
    QMessageBox::information (this, tr ("MTS-ESP"), tr ("libMTS is installed. Restart PHI, then turn on Mode › tuning › send the tuning to mts-esp synths."));
}

bool MixerWindow::mtsEspOn() const { return mtsEsp != nullptr && mtsEsp->running(); }

void MixerWindow::setMtsEsp (bool on)
{
    appSettings().setValue (QStringLiteral ("mixer/mtsEsp"), on);
    if (! on)
    {
        mtsEsp.reset();
        return;
    }
    if (mtsEsp == nullptr)
        mtsEsp = std::make_unique<mixer::MtsEsp>();
    switch (mtsEsp->start())
    {
        case mixer::MtsEsp::Start::started:
            applyTunings();
            say (tr ("MTS-ESP: synths that follow it now play in the tuning in force"));
            return;
        case mixer::MtsEsp::Start::noLibrary:
            installLibMts();
            break;
        case mixer::MtsEsp::Start::restart:
            QMessageBox::information (this, tr ("MTS-ESP"), tr ("libMTS is installed: restart PHI, then turn this on again."));
            break;
        case mixer::MtsEsp::Start::otherMaster:
            QMessageBox::information (this, tr ("MTS-ESP"),
                                      tr ("Another program is the MTS-ESP master now (MTS-ESP Mini, a DAW or a tuning plug-in). Close it, then turn this on again."));
            break;
    }
    mtsEsp.reset();
    appSettings().setValue (QStringLiteral ("mixer/mtsEsp"), false);
}

void MixerWindow::clearSlot (int track, int scene)
{
    if (slots[(size_t) track][(size_t) scene] == nullptr)
        return;
    slots[(size_t) track][(size_t) scene] = nullptr;
    engine.setSlot (track, scene, nullptr);
    changed();
    grid->update();
    rack->refresh();
}

void MixerWindow::placeLauncherMidi()
{
    for (int t = 0; t < mixer::maxTracks; ++t)
        for (int sc = 0; sc < mixer::maxScenes; ++sc)
        {
            const auto& slot = slots[(size_t) t][(size_t) sc];
            if (slot == nullptr || slot->clip.midi == nullptr || slot->clip.midi->song == nullptr)
                continue;
            auto copy = std::make_shared<mixer::Slot> (*slot);
            auto& c = copy->clip;
            const bool whole = c.offset == 0 && c.length == c.midi->length;
            const auto before = std::max<Frame> (1, c.midi->length);
            c.midi = launcherMidi (c.midi->song, c.midi->path);
            if (c.reversed)
                c.midi = mixer::reverseMidi (*c.midi);
            const auto ratio = (double) c.midi->length / (double) before;
            c.offset = whole ? 0 : (Frame) std::llround ((double) c.offset * ratio);
            c.length = whole ? c.midi->length : std::min (c.midi->length - c.offset, (Frame) std::llround ((double) c.length * ratio));
            slots[(size_t) t][(size_t) sc] = copy;
            engine.setSlot (t, sc, copy);
        }
    grid->update();
}

int MixerWindow::trackFor (const QString& trackName, const QColor& colour) const
{
    for (int t = 0; t < engine.numTracks() && ! trackName.isEmpty(); ++t)
        if (info[(size_t) t].name.trimmed().compare (trackName.trimmed(), Qt::CaseInsensitive) == 0)
            return t;
    for (int t = 0; t < engine.numTracks() && colour.isValid(); ++t)
        if (info[(size_t) t].colour.rgb() == colour.rgb())
            return t;
    return -1;
}

void MixerWindow::phiDeal (const QStringList& files, const QStringList& instruments, const QStringList& names,
                           const QList<QColor>& colours, bool launcher, bool again)
{
    // Which track each dealt file goes to. A colour with no track of its own gets the one a
    // mix from the set would give it (the top colour on the last track), added when missing.
    const auto count = std::min ({ (int) files.size(), (int) instruments.size(), (int) names.size(), (int) colours.size(), mixer::maxTracks });
    std::vector<std::pair<int, QString>> deal;   // track, file
    std::vector<std::pair<int, QString>> players;   // track, instrument
    std::set<int> used;
    bool added = false;
    for (int c = 0; c < count; ++c)
    {
        if (files[c].isEmpty() && instruments[c].isEmpty())
            continue;
        auto track = trackFor (names[c], colours[c]);
        if (track < 0)
        {
            track = count - 1 - c;
            if (track >= engine.numTracks())
            {
                engine.setNumTracks (track + 1);
                info[(size_t) track].name = names[c];
                info[(size_t) track].colour = colours[c];
                added = true;
            }
        }
        if (track < 0 || used.count (track) != 0)
            continue;
        used.insert (track);
        if (! instruments[c].isEmpty())
            players.emplace_back (track, instruments[c]);
        if (! files[c].isEmpty())
            deal.emplace_back (track, files[c]);
    }
    if (added)
        rebuildStrips();
    if (deal.empty() && players.empty())
    {
        status->setText (tr ("Φ: nothing to deal. Mark sounds or instruments with the Selector's colours first (1 to 8 in the Find panel)."));
        return;
    }

    // The instruments first, so the MIDI files dealt play on them.
    for (const auto& [track, id] : players)
        setInstrumentKind (track, firstPlugin, id);
    if (! players.empty())
        rack->refresh();
    if (deal.empty())
    {
        status->setText (tr ("Φ dealt %n instrument(s)", nullptr, (int) players.size()));
        return;
    }

    again = again && lastDeal.valid && lastDeal.launcher == launcher;
    if (launcher)
    {
        // The first scene empty on every track dealt to, or the last deal's scene again.
        auto scene = again ? lastDeal.scene : -1;
        for (int s = 0; s < mixer::maxScenes && scene < 0; ++s)
            if (std::all_of (deal.begin(), deal.end(), [&] (const auto& d)
                             { return s >= numScenes || (slots[(size_t) d.first][(size_t) s] == nullptr && loadingSlots.count ({ d.first, s }) == 0); }))
                scene = s;
        if (scene < 0)
        {
            status->setText (tr ("Φ: all %1 scenes are full on these tracks. Empty a scene first.").arg (mixer::maxScenes));
            return;
        }
        for (const auto& [track, file] : deal)
            loadSlot (track, scene, file);
        lastDeal = { true, true, scene, 0, {} };
        moveCursor (deal.front().first, scene);
        status->setText (tr ("Φ dealt %n sound(s) into %1", nullptr, (int) deal.size()).arg (sceneName (scene)));
        return;
    }

    // Onto the tracks: all at one time, after everything already on them, so a second deal
    // follows the first. Again: the clips go back to how they were, and this deal takes its place.
    Frame at = 0;
    if (again)
    {
        restore (lastDeal.before);
        at = lastDeal.at;
    }
    else
    {
        remember();
        for (const auto& d : deal)
            for (const auto& clip : engine.clips (d.first))
                at = std::max (at, clip.end());
    }
    lastDeal = { true, false, -1, at, snapshot() };
    for (const auto& [track, file] : deal)
        placeFiles (track, { file }, at, false);
    status->setText (tr ("Φ dealt %n sound(s) onto the tracks", nullptr, (int) deal.size()));
}

void MixerWindow::sendToLauncher (const QString& file, const QString& trackName, const QColor& colour)
{
    // The track with the Selector colour's name, else with its colour, else the selected one.
    auto track = trackFor (trackName, colour);
    if (track < 0)
        track = selected < mixer::masterIndex ? selected : 0;

    auto scene = firstEmptyScene (track);
    if (scene < 0)
    {
        if (numScenes >= mixer::maxScenes)
        {
            status->setText (tr ("%1 is full: all %2 scenes have a clip. Empty a slot (Delete) first.").arg (this->trackName (track)).arg (mixer::maxScenes));
            return;
        }
        scene = numScenes;   // a scene more
    }
    loadSlot (track, scene, file);
    moveCursor (track, scene);   // the strip and the rack follow
}

bool MixerWindow::launcherKey (QKeyEvent* e)
{
    const auto key = e->key();
    const auto mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    const bool plain = mods == Qt::NoModifier, shift = mods == Qt::ShiftModifier;
    const bool ctrl = mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier);
    const auto track = selected < mixer::masterIndex ? selected : engine.numTracks() - 1;
    auto nudgeFader = [&] (double step)
    {
        auto& s = engine.strip (track);
        const auto from = s.faderDb.load() <= -96.0f ? -60.0 : (double) s.faderDb.load();
        s.faderDb = (float) std::clamp (from + step, -96.0, 6.0);
        strips[(size_t) track]->refresh();
        changed();
    };
    const bool fine = (mods & Qt::ShiftModifier) != 0;
    switch (key)
    {
        case Qt::Key_Left:  if (plain) { moveCursor (std::max (0, track - 1), cursorScene); return true; } break;
        case Qt::Key_Right: if (plain) { moveCursor (std::min (engine.numTracks() - 1, track + 1), cursorScene); return true; } break;
        case Qt::Key_Up:
            if (plain) { moveCursor (track, cursorScene - 1); return true; }
            if (ctrl) { nudgeFader (fine ? 0.1 : 1.0); return true; }
            break;
        case Qt::Key_Down:
            if (plain) { moveCursor (track, cursorScene + 1); return true; }
            if (ctrl) { nudgeFader (fine ? -0.1 : -1.0); return true; }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (plain)
            {
                launchSlot (track, cursorScene);
                return true;
            }
            if (shift)
            {
                const bool wasPlaying = engine.isPlaying();
                engine.launchScene (cursorScene);
                if (! wasPlaying)
                    Q_EMIT playStarted();
                return true;
            }
            break;
        case Qt::Key_Period:
            if (plain) { engine.stopTrack (track); return true; }
            if (shift) { engine.stopAll(); return true; }
            break;
        case Qt::Key_Greater:   // Shift+. on many keyboards
            engine.stopAll();
            return true;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (plain) { clearSlot (track, cursorScene); return true; }
            break;
        case Qt::Key_T:
            if (plain) { engine.backToTimeline (track); return true; }
            if (shift) { engine.backToTimeline (-1); return true; }
            break;
        default: break;
    }
    return false;
}
