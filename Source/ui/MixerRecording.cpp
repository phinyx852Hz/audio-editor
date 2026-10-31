// SPDX-License-Identifier: AGPL-3.0-only
// The mixer's transport and recording (docs/recording.md): the row across the top for both
// the launcher and the tracks across time, the Record cycle, takes into slots and onto the
// timeline, Capture, the audio input, and keeping each take as a file beside the mix.
#include "MixerWindow.h"
#include "InfoStrip.h"
#include "Settings.h"
#include "Theme.h"
#include "TransportBar.h"
#include "../engine/AudioCapture.h"
#include "../engine/AudioEngine.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <optional>

//==============================================================================
// Where the transport is, as bars, beats and sixteenths over minutes and seconds.
class PositionDisplay final : public QWidget
{
public:
    explicit PositionDisplay (QWidget* parent) : QWidget (parent)
    {
        setFocusPolicy (Qt::NoFocus);
        setAccessibleName (MixerWindow::tr ("Position"));
        setToolTip (MixerWindow::tr ("Where the transport is: bar, beat and sixteenth, then minutes and seconds. In the launcher, while "
                                     "clips play or record, it counts the launch clock"));
        setMinimumSize (sizeHint());
    }

    void set (Frame frames, double sampleRate, double bpm, int beatsPerBar, bool launcher)
    {
        sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto seconds = (double) std::max<Frame> (0, frames) / sampleRate;
        const auto beats = seconds * std::max (1.0, bpm) / 60.0;
        const auto bpb = std::max (1, beatsPerBar);
        const auto bar = (long long) (beats / bpb);
        const auto beat = (int) std::fmod (beats, (double) bpb);
        const auto sixteenth = (int) (std::fmod (beats, 1.0) * 4.0);
        const auto ms = (long long) (seconds * 1000.0 + 0.5);
        auto top = QStringLiteral ("%1.%2.%3").arg (bar + 1, 3).arg (beat + 1).arg (sixteenth + 1);
        auto bottom = QString::asprintf ("%lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
        if (launcher)
            bottom = MixerWindow::tr ("clock %1").arg (bottom);
        if (top != bars || bottom != time)
        {
            bars = top;
            time = bottom;
            setAccessibleDescription (bars + QLatin1Char (' ') + time);
            update();
        }
    }

    QSize sizeHint() const override
    {
        const QFontMetricsF big (bigFont()), small (font());
        return { (int) std::ceil (std::max (steady::width (big, QStringLiteral ("000.0.0")), steady::width (small, QStringLiteral ("clock 00:00.000")))) + 12,
                 (int) std::ceil (big.height() + small.height()) + 2 };
    }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::TextAntialiasing);
        const auto& theme = Theme::current();
        g.setPen (theme.color (QStringLiteral ("windowText")));
        g.setFont (bigFont());
        const QFontMetricsF big (g.font());
        steady::draw (g, (width() - steady::width (big, bars)) / 2.0, big.ascent(), bars);
        g.setFont (font());
        g.setPen (theme.color (QStringLiteral ("disabledText")));
        const QFontMetricsF small (g.font());
        steady::draw (g, (width() - steady::width (small, time)) / 2.0, big.height() + small.ascent(), time);
    }

private:
    QFont bigFont() const
    {
        auto f = font();
        f.setPointSizeF (f.pointSizeF() * 1.4);
        f.setBold (true);
        return f;
    }

    QString bars = QStringLiteral ("  1.1.1"), time = QStringLiteral ("0:00.000");
};

namespace
{
    // The audio input, read on the mixer's audio thread from PipeWire's capture buffer (one reader).
    struct CaptureInput final : mixer::Input
    {
        explicit CaptureInput (AudioCapture& c) : capture (c) {}
        int read (float* interleaved, int maxFrames) override { return capture.isRunning() ? capture.read (interleaved, maxFrames) : 0; }
        AudioCapture& capture;
    };

    // The lengths a take can have: no set length, so many bars, or as long as the clips playing.
    constexpr int lengthBars[] { 0, 1, 2, 4, 8, 16, 32, -1 };
    constexpr int numLengths = (int) std::size (lengthBars);
    constexpr double quantiseChoices[] { 0.0, 1.0, 0.5, 0.25, 0.125 };   // beats

    QString lengthName (int index)
    {
        const auto bars = lengthBars[std::clamp (index, 0, numLengths - 1)];
        if (bars == 0)
            return MixerWindow::tr ("Free");
        if (bars < 0)
            return MixerWindow::tr ("Like the loops");
        return MixerWindow::tr ("%n Bar(s)", "", bars);
    }

    QString quantiseChoiceName (double beats)
    {
        if (beats <= 0.0)
            return MixerWindow::tr ("As Played");
        return MixerWindow::tr ("1/%1 Notes").arg ((int) std::lround (4.0 / beats));
    }
}

namespace layers
{
    // A slot's layers as it plays them, from its first frame forwards, so a layer added or
    // overdubbed lines up with them frame for frame.
    std::shared_ptr<mixer::MidiClip> flatMidi (const mixer::Clip& c, size_t room)
    {
        auto copy = std::make_shared<mixer::MidiClip> (*c.midi);
        copy->events.clear();
        copy->events.reserve (c.midi->events.size() + room);
        for (const auto& e : c.midi->events)
            if (e.frame >= c.offset && e.frame < c.offset + c.length)
                copy->events.push_back ({ e.frame - c.offset, e.status, e.data1, e.data2 });
        copy->length = c.length;
        copy->song = nullptr;   // a new file once it is recorded into
        if (c.offset != 0 || c.reversed || c.length != c.midi->length)
            copy->path.clear();   // not the file's notes now: saved again
        return copy;
    }

    std::shared_ptr<mixer::AudioClip> flatAudio (const mixer::Clip& c)
    {
        auto copy = std::make_shared<mixer::AudioClip>();
        copy->mono = c.audio->mono;
        copy->left.resize ((size_t) c.length);
        copy->right.resize ((size_t) c.length);
        const auto last = c.audio->length() - 1;
        for (Frame f = 0; f < c.length; ++f)
        {
            const auto src = (size_t) std::clamp<Frame> (c.reversed ? last - (c.offset + f) : c.offset + f, 0, last);
            copy->left[(size_t) f] = c.audio->left[src];
            copy->right[(size_t) f] = c.audio->right[src];
        }
        copy->path = c.offset == 0 && ! c.reversed && c.length == c.audio->length() ? c.audio->path : std::string();
        return copy;
    }
}

//==============================================================================
QWidget* MixerWindow::buildTransport()
{
    using S = TransportButton::Symbol;
    auto* row = new QWidget (this);
    row->setAccessibleName (tr ("Transport"));
    auto* h = new QHBoxLayout (row);
    h->setContentsMargins (4, 2, 4, 2);
    h->setSpacing (4);
    auto settings = appSettings();

    auto* startButton = new TransportButton (S::start, tr ("To the start (Home)"), row);
    playButton = new TransportButton (S::play, tr ("Play, or pause where it is (Space)"), row);
    auto* stopButton = new TransportButton (S::stop, tr ("Stop: the transport and every clip; pressed again, to the start"), row);
    auto* endButton = new TransportButton (S::end, tr ("To the end of the mix (End)"), row);
    recordButton = new TransportButton (S::record, tr ("Record (Ctrl+R) on the armed track. In the launcher: into its slot under the cursor "
                                                       "(or the next empty one), again to stop, again to overdub the loop, again to stop the "
                                                       "overdub. In the tracks across time: from the playhead, again to stop"), row);
    recordButton->setCheckable (true);
    loopButton = new TransportButton (S::loop, tr ("Loop the whole mix (L)"), row);
    loopButton->setCheckable (true);
    positionDisplay = new PositionDisplay (row);

    beatsBox = new QSpinBox (row);
    beatsBox->setRange (1, 12);
    beatsBox->setValue (4);
    beatsBox->setSuffix (QStringLiteral (" / 4"));
    beatsBox->setToolTip (tr ("The time signature: beats in a bar. Bars, the metronome's first beat, record lengths and Capture follow it"));
    beatsBox->setFocusPolicy (Qt::ClickFocus);

    auto* click = new QPushButton (tr ("Click"), row);
    click->setCheckable (true);
    click->setFocusPolicy (Qt::NoFocus);
    click->setToolTip (tr ("The metronome while the transport plays and during a count-in (C)"));
    click->setChecked (settings.value (QStringLiteral ("mixer/metronome"), false).toBool());
    engine.metronome = click->isChecked();
    clickButton = click;

    countInBox = new QComboBox (row);
    countInBox->setFocusPolicy (Qt::NoFocus);
    countInBox->addItems ({ tr ("No Count-in"), tr ("Count-in 1 Bar"), tr ("Count-in 2 Bars") });
    countInBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("mixer/countIn"), 0).toInt(), 0, 2));
    countInBox->setToolTip (tr ("Clicks before a take recorded with the transport stopped"));

    recordLengthBox = new QComboBox (row);
    recordLengthBox->setFocusPolicy (Qt::NoFocus);
    for (int i = 0; i < numLengths; ++i)
        recordLengthBox->addItem (tr ("Rec %1").arg (lengthName (i)));
    recordLengthBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("mixer/recordLength"), 3).toInt(), 0, numLengths - 1));
    recordLengthBox->setToolTip (tr ("How long a take into a slot is: so many bars (it stops by itself and loops), Free (it stops on the next "
                                     "quantise after Record is pressed again), or as long as the clips playing"));

    auto* captureButton = new QPushButton (tr ("Capture"), row);
    captureButton->setFocusPolicy (Qt::NoFocus);
    captureButton->setToolTip (tr ("Capture (Ctrl+Shift+R): what you just played on the armed track, recording or not, into its next empty "
                                   "slot, in whole bars, playing in time"));
    auto* options = new QToolButton (row);
    options->setText (tr ("Record Options"));
    options->setFocusPolicy (Qt::NoFocus);
    options->setToolTip (tr ("What happens after a take, quantise, the audio input and the armed track's input and monitoring"));

    afterRecording = (After) std::clamp (settings.value (QStringLiteral ("mixer/afterRecording"), 0).toInt(), 0, 3);
    recordQuantise = settings.value (QStringLiteral ("mixer/recordQuantise"), 0.0).toDouble();
    inputSource = settings.value (QStringLiteral ("mixer/inputSource")).toString();
    engine.inputLatencyMs = (float) settings.value (QStringLiteral ("mixer/inputLatencyMs"), 10.0).toDouble();

    for (auto* w : std::initializer_list<QWidget*> { startButton, playButton, stopButton, endButton, recordButton, loopButton })
        h->addWidget (w);
    h->addSpacing (8);
    h->addWidget (positionDisplay);
    h->addSpacing (8);
    h->addWidget (tempoBox);
    h->addWidget (beatsBox);
    h->addWidget (click);
    h->addWidget (countInBox);
    h->addWidget (quantiseBox);
    h->addWidget (recordLengthBox);
    h->addWidget (captureButton);
    h->addWidget (options);
    h->addWidget (followTempoBox);
    h->addStretch (1);

    connect (startButton, &QToolButton::clicked, this, [this] { toStart(); });
    connect (playButton, &QToolButton::clicked, this, [this] { togglePlay(); });
    connect (stopButton, &QToolButton::clicked, this, [this]
    {
        if (engine.isPlaying() || engine.countingIn())
            engine.stop();
        else
            toStart();
        refreshTransport();
    });
    connect (endButton, &QToolButton::clicked, this, [this]
    {
        engine.setPosition (engine.length());
        refreshTransport();
    });
    connect (recordButton, &QToolButton::clicked, this, [this] { recordPressed(); });
    connect (beatsBox, &QSpinBox::valueChanged, this, [this] (int beats)
    {
        engine.beatsPerBar = beats;
        refreshViews();
        changed();
    });
    connect (click, &QPushButton::toggled, this, [this] (bool on)
    {
        engine.metronome = on;
        appSettings().setValue (QStringLiteral ("mixer/metronome"), on);
    });
    connect (countInBox, &QComboBox::activated, this, [] (int i) { appSettings().setValue (QStringLiteral ("mixer/countIn"), i); });
    connect (recordLengthBox, &QComboBox::activated, this, [] (int i) { appSettings().setValue (QStringLiteral ("mixer/recordLength"), i); });
    connect (captureButton, &QPushButton::clicked, this, [this] { captureMidi(); });
    connect (options, &QToolButton::clicked, this, [this, options] { recordMenu (options->mapToGlobal (QPoint (0, options->height()))); });
    return row;
}

//==============================================================================
mixer::RecordRequest MixerWindow::recordRequest (int track, int scene) const
{
    mixer::RecordRequest q;
    q.track = track;
    q.scene = scene;
    const auto& s = engine.strip (track);
    switch (s.input.load())
    {
        case mixer::Strip::inputMidi: q.midi = true; q.audio = false; break;
        case mixer::Strip::inputAudio: q.midi = false; q.audio = true; break;
        case mixer::Strip::inputBoth: q.midi = true; q.audio = true; break;
        case mixer::Strip::inputInstrument: q.midi = false; q.audio = true; q.fromInstrument = true; break;
        default:
            // The keyboard on a track with an instrument, else the audio input.
            q.midi = engine.instrument (track) != nullptr && (mixer::TrackMode) s.mode.load() != mixer::TrackMode::audio;
            q.audio = ! q.midi;
            break;
    }
    const auto bpb = std::max (1, engine.beatsPerBar.load());
    const auto bars = lengthBars[std::clamp (recordLengthBox->currentIndex(), 0, numLengths - 1)];
    q.beats = bars > 0 ? (double) bars * bpb : 0.0;
    if (bars < 0)
    {
        // As long as the longest clip the launcher plays.
        Frame longest = 0;
        for (int t = 0; t < engine.numTracks(); ++t)
            if (const auto launch = engine.launchInfo (t); launch.playing >= 0)
                if (const auto slot = engine.slot (t, launch.playing))
                    longest = std::max (longest, slot->clip.length);
        q.beats = longest > 0 ? (double) longest / rate() * engine.tempo.load() / 60.0 : 0.0;
    }
    if (scene < 0)
        q.beats = 0.0;   // onto the timeline: until stopped
    q.quantise = recordQuantise;
    q.playAfter = afterRecording != After::stop;
    q.maxSeconds = q.audio && scene >= 0 ? 180.0 : 600.0;
    return q;
}

int MixerWindow::nextSlotFor (int track, int from) const
{
    for (int s = std::clamp (from, 0, numScenes - 1); s < numScenes; ++s)
        if (slots[(size_t) track][(size_t) s] == nullptr && loadingSlots.count ({ track, s }) == 0
            && ! (engine.recordInfo (track).state != mixer::RecordState::none && engine.recordInfo (track).scene == s))
            return s;
    return numScenes < mixer::maxScenes ? numScenes : -1;
}

bool MixerWindow::startTake (mixer::RecordRequest q, QString& error)
{
    if ((q.audio || q.overdubAudio != nullptr) && ! q.fromInstrument && ! ensureInput (error))
        return false;
    if (q.scene >= numScenes)
        setScenes (q.scene + 1);
    std::string why;
    if (! engine.record (q, why))
    {
        error = QString::fromStdString (why);
        return false;
    }
    if (! engine.isPlaying() && ! engine.countingIn())
    {
        const auto bars = countInBox->currentIndex();
        engine.playWithCountIn (bars);
        Q_EMIT playStarted();
    }
    refreshRecording();
    updateInput();
    return true;
}

void MixerWindow::recordPressed (int intoLauncher)
{
    const auto track = engine.liveTrack();
    if (track < 0 || track >= engine.numTracks())
    {
        say (tr ("Arm a track to record on first (R)."));
        refreshRecording();
        return;
    }
    const auto now = engine.recordInfo (track);
    if (now.state == mixer::RecordState::waiting)
    {
        engine.stopRecording (track);
        say (tr ("The take on %1 was cancelled before it began.").arg (trackName (track)));
    }
    else if (now.state == mixer::RecordState::recording)
    {
        engine.stopRecording (track);
        say (now.scene >= 0 && now.length > 0 ? tr ("%1 ends its take by itself at its length; no more follow it.").arg (trackName (track))
             : now.scene >= 0              ? tr ("%1 stops recording on the next quantise.").arg (trackName (track))
                                           : tr ("%1 stopped recording.").arg (trackName (track)));
    }
    else if (now.state == mixer::RecordState::overdubbing)
    {
        engine.stopRecording (track);
        say (tr ("Overdub on %1 stopped; the loop plays on.").arg (trackName (track)));
    }
    else if (intoLauncher < 0 ? launcherShown() : intoLauncher == 1)
    {
        if (const auto playing = engine.launchInfo (track).playing; playing >= 0)
        {
            // A loop of one layer gets the other one; else it is overdubbed.
            const auto slot = engine.slot (track, playing);
            const bool sound = recordsAudioLayer (track);
            if (slot != nullptr && (sound ? slot->clip.audio == nullptr : slot->clip.midi == nullptr))
                recordLayer (track, playing, sound);
            else
                toggleOverdub (track);
        }
        else
            recordSlot (track, nextSlotFor (track, selected == track ? cursorScene : 0));
    }
    else
        recordTimeline (track);
    refreshRecording();
}

void MixerWindow::recordSlot (int track, int scene)
{
    if (track < 0 || track >= engine.numTracks())
        return;
    if (scene < 0)
    {
        say (tr ("%1 has no empty slot left to record into.").arg (trackName (track)));
        return;
    }
    if (engine.liveTrack() != track)
        arm (track);   // a slot's record dot arms its track
    auto q = recordRequest (track, scene);
    QString error;
    if (! startTake (q, error))
    {
        say (error);
        return;
    }
    // Record Next: the following take asked for at once, so it starts the frame this one ends.
    if (afterRecording == After::recordNext && q.beats >= 0.0)
    {
        auto next = recordRequest (track, nextSlotFor (track, scene + 1));
        if (next.scene >= 0)
        {
            if (next.scene >= numScenes)
                setScenes (next.scene + 1);
            std::string why;
            engine.record (next, why);
        }
    }
    moveCursor (track, scene);
    say (q.beats > 0.0 ? tr ("Recording %1 into %2 for %3 bars.").arg (trackName (track), sceneName (scene))
                             .arg (q.beats / std::max (1, engine.beatsPerBar.load()), 0, 'g', 3)
                       : tr ("Recording %1 into %2; Record again stops it on the next quantise.").arg (trackName (track), sceneName (scene)));
    refreshViews();
}

void MixerWindow::toggleOverdub (int track)
{
    const auto launch = engine.launchInfo (track);
    const auto scene = launch.playing;
    const auto slot = scene >= 0 ? engine.slot (track, scene) : nullptr;
    if (slot == nullptr || slot->clip.length <= 0)
        return;
    // The slot gets its own copy of what it plays, as it plays it, to write into: the layer the
    // track records, when it has both.
    const auto& c = slot->clip;
    auto overdubbed = std::make_shared<mixer::Slot> (*slot);
    overdubbed->clip.offset = 0;
    overdubbed->clip.reversed = false;
    auto q = recordRequest (track, scene);
    q.overdub = true;
    const bool sound = c.audio != nullptr && (c.midi == nullptr || recordsAudioLayer (track));
    if (! sound)
    {
        auto copy = layers::flatMidi (c, 16384);
        overdubbed->clip.midi = copy;
        q.overdubMidi = copy;
        q.midi = true;
        q.audio = false;
        q.fromInstrument = false;
    }
    else
    {
        auto copy = layers::flatAudio (c);
        copy->path.clear();   // a new file once the overdub ends
        overdubbed->clip.audio = copy;
        q.overdubAudio = copy;
        q.midi = false;
        q.audio = true;
    }
    // The other layer goes along as it plays.
    if (sound && c.midi != nullptr && (c.offset != 0 || c.reversed))
        overdubbed->clip.midi = layers::flatMidi (c, 0);
    if (! sound && c.audio != nullptr && (c.offset != 0 || c.reversed))
        overdubbed->clip.audio = layers::flatAudio (c);
    slots[(size_t) track][(size_t) scene] = overdubbed;
    engine.setSlot (track, scene, overdubbed);
    QString error;
    if (! startTake (q, error))
    {
        say (error);
        return;
    }
    say (tr ("Overdubbing %1 in %2: Record again to stop.").arg (trackName (track), sceneName (scene)));
}

bool MixerWindow::recordsAudioLayer (int track) const
{
    const auto q = recordRequest (track, 0);
    return q.audio && ! q.midi;
}

void MixerWindow::recordLayer (int track, int scene, bool wantAudio)
{
    if (track < 0 || track >= engine.numTracks() || scene < 0 || scene >= mixer::maxScenes)
        return;
    const auto slot = engine.slot (track, scene);
    if (slot == nullptr || slot->clip.length <= 0)
        return;
    const auto& c = slot->clip;
    if (wantAudio ? c.audio != nullptr : c.midi != nullptr)
    {
        say (wantAudio ? tr ("%1 already has its audio layer.").arg (sceneName (scene)) : tr ("%1 already has its MIDI layer.").arg (sceneName (scene)));
        return;
    }
    if (engine.recordInfo (track).state != mixer::RecordState::none)
    {
        say (tr ("%1 is recording already.").arg (trackName (track)));
        return;
    }
    if (engine.liveTrack() != track)
        arm (track);
    auto q = recordRequest (track, scene);
    q.layer = true;
    q.overdub = false;
    q.beats = 0.0;   // the loop's own length
    // The slot, as it plays, with its new half empty.
    auto layered = std::make_shared<mixer::Slot> (*slot);
    layered->clip.offset = 0;
    layered->clip.reversed = false;
    if (c.midi != nullptr && (c.offset != 0 || c.reversed))
        layered->clip.midi = layers::flatMidi (c, 0);
    if (c.audio != nullptr && (c.offset != 0 || c.reversed))
        layered->clip.audio = layers::flatAudio (c);
    const auto& s = engine.strip (track);
    if (wantAudio)
    {
        // The audio input when the track records it, else the track's own instrument.
        const auto input = s.input.load();
        q.fromInstrument = engine.instrument (track) != nullptr && input != mixer::Strip::inputAudio && input != mixer::Strip::inputBoth;
        auto sound = std::make_shared<mixer::AudioClip>();
        sound->left.assign ((size_t) c.length, 0.0f);
        sound->right.assign ((size_t) c.length, 0.0f);
        sound->mono = ! q.fromInstrument && s.inputChannels.load() != 0;
        layered->clip.audio = sound;
        q.overdubAudio = sound;
        q.audio = true;
        q.midi = false;
    }
    else
    {
        auto notes = std::make_shared<mixer::MidiClip>();
        notes->events.reserve (16384);
        notes->bpm = engine.tempo.load();
        notes->length = c.length;
        layered->clip.midi = notes;
        q.overdubMidi = notes;
        q.midi = true;
        q.audio = false;
        q.fromInstrument = false;
        giveSynth (track);
    }
    slots[(size_t) track][(size_t) scene] = layered;
    engine.setSlot (track, scene, layered);
    QString error;
    if (! startTake (q, error))
    {
        slots[(size_t) track][(size_t) scene] = slot;   // as it was
        engine.setSlot (track, scene, slot);
        say (error);
        return;
    }
    layerTakes[(size_t) track] = { scene, wantAudio };
    giveBothLayers (track);
    moveCursor (track, scene);
    say (wantAudio ? (q.fromInstrument ? tr ("Recording %1's instrument as audio under %2 for one pass.").arg (trackName (track), sceneName (scene))
                                       : tr ("Recording audio under %1 for one pass.").arg (sceneName (scene)))
                   : tr ("Recording MIDI over %1 for one pass.").arg (sceneName (scene)));
    refreshViews();
}

void MixerWindow::recordTimeline (int track)
{
    auto q = recordRequest (track, -1);
    QString error;
    timelinePasses[(size_t) track].clear();
    if (! startTake (q, error))
    {
        say (error);
        return;
    }
    say (tr ("Recording %1 from the playhead; Record or Stop ends it.").arg (trackName (track)));
}

void MixerWindow::captureMidi()
{
    const auto track = engine.liveTrack();
    if (track < 0 || track >= engine.numTracks())
    {
        say (tr ("Arm the track you played on first (R)."));
        return;
    }
    Frame phase = 0;
    const auto clip = engine.capture (track, phase);
    if (clip == nullptr)
    {
        say (tr ("Nothing played on %1 to capture.").arg (trackName (track)));
        return;
    }
    const auto scene = nextSlotFor (track, 0);
    if (scene < 0)
    {
        say (tr ("%1 has no empty slot left for the capture.").arg (trackName (track)));
        return;
    }
    if (scene >= numScenes)
        setScenes (scene + 1);
    mixer::Recorded rec;
    rec.track = track;
    rec.scene = scene;
    rec.midi = clip;
    rec.length = clip->length;
    rec.notes = true;
    QString error;
    const auto file = saveTake (rec, error);
    if (file.isEmpty())
    {
        say (error);
        return;
    }
    auto slot = std::make_shared<mixer::Slot>();
    slot->clip = mixer::makeClip (mixer::MidiClipPtr (clip), 0);
    slots[(size_t) track][(size_t) scene] = slot;
    engine.setSlot (track, scene, slot);
    giveSynth (track);
    if (engine.isPlaying())
        engine.launchAt (track, scene, phase);   // in time with when it was played
    else
    {
        engine.launch (track, scene);
        Q_EMIT playStarted();
    }
    moveCursor (track, scene);
    refreshViews();
    changed();
    say (tr ("Captured %n bar(s) from %1 into %2.", "", (int) std::lround ((double) clip->length / rate() * engine.tempo.load() / 60.0
                                                                          / std::max (1, engine.beatsPerBar.load())))
             .arg (trackName (track), sceneName (scene)));
}

QString MixerWindow::saveTake (const mixer::Recorded& rec, QString& error)
{
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = tr ("%1 take").arg (trackName (rec.track));
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    const auto ext = rec.audio != nullptr ? QStringLiteral (".wav") : QStringLiteral (".mid");
    QString file;
    for (; file.isEmpty() || QFileInfo::exists (file); ++takeNumber)
        file = QDir (base).filePath (QStringLiteral ("%1 %2%3").arg (stem).arg (takeNumber).arg (ext));
    if (rec.audio != nullptr)
    {
        // What the audio thread wrote, as long as the take.
        const auto& a = *rec.audio;
        const auto frames = (int) std::min<Frame> (rec.length > 0 ? rec.length : a.length(), a.length());
        AudioData::Builder b (a.mono ? 1 : 2, rate());
        const float* ptrs[2] = { a.left.data(), a.right.data() };
        b.append (ptrs, frames);
        SaveOptions options;
        options.bitDepth = 32;
        std::string why;
        if (! AudioEngine::saveFile (file.toStdString(), *b.finish(), {}, {}, options, why))
        {
            error = tr ("The take could not be saved to %1: %2").arg (file, QString::fromStdString (why));
            return {};
        }
        // The audio thread never reads a clip's path.
        std::const_pointer_cast<mixer::AudioClip> (rec.audio)->path = file.toStdString();
        return file;
    }
    if (rec.midi != nullptr)
    {
        const auto song = mixer::songOf (*rec.midi, rate(), rec.midi->bpm > 0.0 ? rec.midi->bpm : engine.tempo.load());
        std::string why;
        if (! midifile::write (*song, file.toStdString(), why))
        {
            error = tr ("The take could not be saved to %1: %2").arg (file, QString::fromStdString (why));
            return {};
        }
        // Nor its path and song, which a change of tempo places the notes from again.
        auto clip = std::const_pointer_cast<mixer::MidiClip> (rec.midi);
        clip->path = file.toStdString();
        clip->song = song;
        return file;
    }
    error = tr ("The take had nothing in it.");
    return {};
}

void MixerWindow::keepRecorded()
{
    const auto taken = engine.takeRecorded();
    // The editor's track's newest take opens in the clip editor: the slot, or the timeline clip.
    int openScene = -1;
    uint32_t openClip = 0;
    for (const auto& rec : taken)
    {
        const auto track = rec.track;
        if (track < 0 || track >= engine.numTracks())
            continue;
        QString error;
        if (rec.layer)
        {
            // A layer: kept in the slot with the other, or taken out again when nothing came of it.
            const auto scene = rec.scene;
            layerTakes[(size_t) track] = {};
            auto slot = engine.slot (track, scene);
            if (rec.length <= 0 || (rec.audio == nullptr && ! rec.notes))
            {
                if (slot != nullptr && ((rec.audio != nullptr && slot->clip.audio == rec.audio) || (rec.midi != nullptr && slot->clip.midi == rec.midi)))
                {
                    auto back = std::make_shared<mixer::Slot> (*slot);
                    if (rec.audio != nullptr)
                        back->clip.audio = nullptr;
                    else
                        back->clip.midi = nullptr;
                    slots[(size_t) track][(size_t) scene] = back;
                    engine.setSlot (track, scene, back);
                }
                say (rec.length <= 0 ? tr ("The layer was cancelled before it began.") : tr ("Nothing was played in the layer, so nothing was kept."));
                continue;
            }
            // The other layer, if it was flattened for this one, is a file of its own too.
            if (slot != nullptr)
            {
                mixer::Recorded other;
                other.track = track;
                other.scene = scene;
                other.length = slot->clip.length;
                if (rec.audio != nullptr)
                    other.midi = slot->clip.midi;
                else
                    other.audio = slot->clip.audio;
                if ((other.audio != nullptr && other.audio->path.empty()) || (other.midi != nullptr && other.midi->path.empty()))
                    saveTake (other, error);
            }
            const auto file = saveTake (rec, error);
            slots[(size_t) track][(size_t) scene] = engine.slot (track, scene);
            if (! file.isEmpty() && track == editorTrack())
                openScene = scene, openClip = 0;
            if (file.isEmpty())
                say (error);
            else
                say (rec.audio != nullptr ? tr ("Audio layer recorded under %1, %2: %3.").arg (trackName (track), sceneName (scene), QFileInfo (file).fileName())
                                          : tr ("MIDI layer recorded over %1, %2: %3.").arg (trackName (track), sceneName (scene), QFileInfo (file).fileName()));
            changed();
            continue;
        }
        if (rec.scene >= 0)
        {
            const auto scene = rec.scene;
            if (rec.audio == nullptr && ! rec.notes && ! rec.overdub)
            {
                // A MIDI take with no note in it: nothing kept.
                slots[(size_t) track][(size_t) scene] = nullptr;
                engine.setSlot (track, scene, nullptr);
                say (tr ("Nothing was played in the take, so nothing was kept."));
                continue;
            }
            const auto file = saveTake (rec, error);
            slots[(size_t) track][(size_t) scene] = engine.slot (track, scene);
            if (! file.isEmpty() && track == editorTrack())
                openScene = scene, openClip = 0;
            if (file.isEmpty())
                say (error);
            else
                say (rec.overdub ? tr ("Overdub kept in %1, %2.").arg (trackName (track), sceneName (scene))
                                 : tr ("Recorded into %1, %2: %3.").arg (trackName (track), sceneName (scene), QFileInfo (file).fileName()));
            if (rec.midi != nullptr)
                giveSynth (track);
            changed();
            if (rec.overdub)
                continue;
            // Record Next: the next take follows the one now recording, frame to frame.
            const auto now = engine.recordInfo (track);
            if (afterRecording == After::recordNext && now.state == mixer::RecordState::recording && now.scene >= 0)
            {
                auto next = recordRequest (track, nextSlotFor (track, now.scene + 1));
                if (next.scene >= 0)
                {
                    if (next.scene >= numScenes)
                        setScenes (next.scene + 1);
                    std::string why;
                    engine.record (next, why);
                }
            }
            if (afterRecording == After::counterpoint && rec.midi != nullptr && rec.notes)
                if (const auto slot = engine.slot (track, scene))
                    autoCounterpoint (track, scene, slot, rec.start + rec.length);
            continue;
        }

        // Onto the timeline: a clip of each kind the take has, at its start. A take over a loop
        // keeps its newest pass on the track; each pass is a file of its own.
        if (rec.pass == 0)
            remember();
        auto list = engine.clips (track);
        auto& passes = timelinePasses[(size_t) track];
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const mixer::Clip& c)
        {
            return std::find (passes.begin(), passes.end(), c.id) != passes.end();
        }), list.end());
        passes.clear();
        QStringList kept;
        auto keep = [&] (mixer::Recorded one)
        {
            const auto file = saveTake (one, error);
            if (file.isEmpty())
                return;
            kept << QFileInfo (file).fileName();
            auto clip = one.audio != nullptr ? mixer::makeClip (one.audio, rec.start) : mixer::makeClip (one.midi, rec.start);
            clip.length = std::min (clip.length, rec.length);
            clip.id = nextClipId++;
            passes.push_back (clip.id);
            list.push_back (clip);
            if (track == editorTrack())
                openScene = -1, openClip = clip.id;   // the MIDI one when the take has both, as it comes last
        };
        if (rec.audio != nullptr)
        {
            auto one = rec;
            one.midi = nullptr;
            keep (one);
        }
        if (rec.midi != nullptr && rec.notes)
        {
            auto one = rec;
            one.audio = nullptr;
            keep (one);
            giveSynth (track);
        }
        editClips (track, list);
        say (kept.isEmpty() ? (error.isEmpty() ? tr ("Nothing was recorded.") : error)
                            : rec.pass > 0 ? tr ("Pass %1 on %2 kept: %3; the earlier passes stay as files.").arg (rec.pass + 1).arg (trackName (track), kept.join (QStringLiteral (", ")))
                                           : tr ("Recorded onto %1: %2.").arg (trackName (track), kept.join (QStringLiteral (", "))));
    }
    if (! taken.empty())
    {
        refreshViews();
        updateInput();
    }
    if (openClip != 0)
        showTimelineClip (editorTrack(), openClip);
    else if (openScene >= 0)
        showSlotClip (editorTrack(), openScene);
    if (openClip != 0 || openScene >= 0)
        refreshEditorTrack();
    refreshRecording();
}

void MixerWindow::showPosition (Frame frames, bool launchClock)
{
    positionDisplay->set (frames, rate(), tempoBox->value(), engine.beatsPerBar.load(), launchClock);
}

void MixerWindow::refreshRecording()
{
    bool any = false;
    for (int t = 0; t < engine.numTracks(); ++t)
        any = any || engine.recordInfo (t).state != mixer::RecordState::none;
    if (recordButton != nullptr && recordButton->isChecked() != any)
        recordButton->setChecked (any);
}

void MixerWindow::recordMenu (QPoint at)
{
    QMenu menu (this);
    auto settings = appSettings();

    auto* after = menu.addMenu (tr ("&After a Take"));
    auto* afterGroup = new QActionGroup (after);
    const std::pair<After, QString> afters[] {
        { After::play, tr ("&Play It (Loop)") },
        { After::stop, tr ("&Stop the Track") },
        { After::recordNext, tr ("Play It and Record the &Next Slot") },
        { After::counterpoint, tr ("Play It and Add &Counterpoint") },
    };
    for (const auto& [kind, name] : afters)
    {
        auto* a = after->addAction (name, this, [this, kind = kind]
        {
            afterRecording = kind;
            appSettings().setValue (QStringLiteral ("mixer/afterRecording"), (int) kind);
        });
        a->setCheckable (true);
        a->setChecked (afterRecording == kind);
        afterGroup->addAction (a);
    }

    auto* quantise = menu.addMenu (tr ("&Quantise Notes"));
    auto* quantiseGroup = new QActionGroup (quantise);
    for (const auto beats : quantiseChoices)
    {
        auto* a = quantise->addAction (quantiseChoiceName (beats), this, [this, beats]
        {
            recordQuantise = beats;
            appSettings().setValue (QStringLiteral ("mixer/recordQuantise"), beats);
        });
        a->setCheckable (true);
        a->setChecked (std::abs (recordQuantise - beats) < 1.0e-9);
        quantiseGroup->addAction (a);
    }

    // The armed track's input and monitoring.
    const auto track = engine.liveTrack();
    if (track >= 0 && track < engine.numTracks())
    {
        menu.addSeparator();
        auto& s = engine.strip (track);
        auto* input = menu.addMenu (tr ("%1 &Records").arg (trackName (track)));
        auto* inputGroup = new QActionGroup (input);
        const std::pair<int, QString> inputs[] {
            { mixer::Strip::inputAuto, tr ("&Auto: the Keyboard with an Instrument, else Audio") },
            { mixer::Strip::inputMidi, tr ("The MIDI &Keyboard") },
            { mixer::Strip::inputAudio, tr ("The &Audio Input") },
            { mixer::Strip::inputBoth, tr ("&Both (the Timeline Takes Both)") },
            { mixer::Strip::inputInstrument, tr ("Its &Instrument, as Audio") },
        };
        for (const auto& [value, name] : inputs)
        {
            auto* a = input->addAction (name, this, [this, &s, value = value] { s.input = value; changed(); updateInput(); });
            a->setCheckable (true);
            a->setChecked (s.input.load() == value);
            inputGroup->addAction (a);
        }
        auto* channels = menu.addMenu (tr ("%1 Input &Channels").arg (trackName (track)));
        auto* channelGroup = new QActionGroup (channels);
        const std::pair<int, QString> sides[] { { 0, tr ("&Stereo") }, { 1, tr ("&Left Alone (Mono)") }, { 2, tr ("&Right Alone (Mono)") } };
        for (const auto& [value, name] : sides)
        {
            auto* a = channels->addAction (name, this, [this, &s, value = value] { s.inputChannels = value; changed(); });
            a->setCheckable (true);
            a->setChecked (s.inputChannels.load() == value);
            channelGroup->addAction (a);
        }
        auto* monitor = menu.addMenu (tr ("%1 &Monitoring").arg (trackName (track)));
        auto* monitorGroup = new QActionGroup (monitor);
        const std::pair<int, QString> monitors[] {
            { mixer::Strip::monitorAuto, tr ("&Auto: Heard While Armed to Record Audio") },
            { mixer::Strip::monitorOn, tr ("Always &On") },
            { mixer::Strip::monitorOff, tr ("O&ff") },
        };
        for (const auto& [value, name] : monitors)
        {
            auto* a = monitor->addAction (name, this, [this, &s, value = value] { s.monitor = value; changed(); updateInput(); });
            a->setCheckable (true);
            a->setChecked (s.monitor.load() == value);
            monitorGroup->addAction (a);
        }
    }

    // The audio input: a microphone or line in, the desktop or one application (PipeWire).
    menu.addSeparator();
    auto* sources = menu.addMenu (tr ("Audio &Input"));
    if (! AudioCapture::isAvailable())
        sources->addAction (tr ("This build of PHI has no PipeWire, so no audio input"))->setEnabled (false);
    else
    {
        if (capture == nullptr)
        {
            capture = std::make_unique<AudioCapture>();
            captureInput = std::make_unique<CaptureInput> (*capture);
        }
        if (! capture->isConnected())
            capture->connect();
        auto* group = new QActionGroup (sources);
        auto add = [&] (const QString& target, const QString& name)
        {
            auto* a = sources->addAction (name, this, [this, target]
            {
                inputSource = target;
                appSettings().setValue (QStringLiteral ("mixer/inputSource"), target);
                if (capture != nullptr && capture->isRunning())
                {
                    engine.setInput (nullptr);
                    capture->stop();
                }
                updateInput();
            });
            a->setCheckable (true);
            a->setChecked (inputSource == target);
            group->addAction (a);
        };
        add ({}, tr ("The System's Default Input (as the desktop's sound settings have it)"));
        for (const auto& source : capture->sources())
        {
            const auto kind = source.kind == CaptureSource::Kind::input ? tr ("Input") : source.kind == CaptureSource::Kind::desktop ? tr ("Desktop")
                                                                                                                                    : tr ("Application");
            add (QString::fromStdString (source.target), tr ("%1: %2").arg (kind, QString::fromStdString (source.name)));
        }
    }
    menu.addAction (tr ("Input &Delay... (%1 ms)").arg (engine.inputLatencyMs.load(), 0, 'f', 1), this, [this]
    {
        bool ok = false;
        const auto ms = QInputDialog::getDouble (this, tr ("Input Delay"),
                                                 tr ("How late the audio input arrives, in milliseconds. Takes are moved earlier by it, so what "
                                                     "you play lines up with what you heard:"),
                                                 engine.inputLatencyMs.load(), 0.0, 500.0, 1, &ok);
        if (ok)
        {
            engine.inputLatencyMs = (float) ms;
            appSettings().setValue (QStringLiteral ("mixer/inputLatencyMs"), ms);
        }
    });
    menu.exec (at);
}

bool MixerWindow::ensureInput (QString& error)
{
    if (! AudioCapture::isAvailable())
    {
        error = tr ("This build of PHI has no PipeWire, so it cannot record audio. MIDI still records.");
        return false;
    }
    if (capture == nullptr)
    {
        capture = std::make_unique<AudioCapture>();
        captureInput = std::make_unique<CaptureInput> (*capture);
    }
    if (! capture->isConnected())
        if (const auto why = capture->connect(); ! why.empty())
        {
            error = tr ("The audio input could not be opened: %1").arg (QString::fromStdString (why));
            return false;
        }
    if (capture->isRunning())
        return true;
    // The source chosen, else the system's default input (as the desktop's sound settings have
    // it). It was the first input by name, which could be a webcam's or the board's microphone
    // rather than the interface, heard as noise on an armed track (owner, 2026-10-30).
    std::optional<CaptureSource> chosen;
    if (inputSource.isEmpty())
        chosen = CaptureSource { CaptureSource::Kind::input, {}, tr ("the default input").toStdString(), {}, 0 };
    for (const auto& s : capture->sources())
        if (! inputSource.isEmpty() && QString::fromStdString (s.target) == inputSource)
            chosen = s;
    if (! chosen.has_value())
    {
        error = inputSource.isEmpty() ? tr ("There is no audio input to record from.") : tr ("The audio input chosen is not there now.");
        return false;
    }
    if (const auto why = capture->start (*chosen, (int) std::lround (rate()), 2); ! why.empty())
    {
        error = tr ("The audio input could not start: %1").arg (QString::fromStdString (why));
        return false;
    }
    engine.setInput (captureInput.get());
    return true;
}

void MixerWindow::updateInput()
{
    // Running while a track records audio or hears its input; else stopped, so PipeWire can rest.
    bool wanted = false;
    for (int t = 0; t < engine.numTracks(); ++t)
    {
        const auto& s = engine.strip (t);
        const auto take = engine.recordInfo (t);
        const auto q = recordRequest (t, 0);
        const bool audioTrack = q.audio && ! q.fromInstrument;
        wanted = wanted || s.monitor.load() == mixer::Strip::monitorOn
              || (audioTrack && (take.state != mixer::RecordState::none || (t == engine.liveTrack() && s.monitor.load() == mixer::Strip::monitorAuto)));
    }
    if (wanted && (capture == nullptr || ! capture->isRunning()))
    {
        QString error;
        if (! ensureInput (error) && engine.recordInfo (engine.liveTrack()).state != mixer::RecordState::none)
            say (error);
    }
    else if (! wanted && capture != nullptr && capture->isRunning())
    {
        engine.setInput (nullptr);   // waits for the audio thread to let go of it
        capture->stop();
    }
}
