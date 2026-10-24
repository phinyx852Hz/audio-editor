// SPDX-License-Identifier: AGPL-3.0-only
// The Tracker menu: MilkyTracker's module player (MilkyPlay), played through PHI and
// recorded into the editor, stereo or one stereo pair per mixer track.
#include "MainWindow.h"
#include "Settings.h"
#include "TrackerWindow.h"
#include "WaveformView.h"
#include "PatchBayWindow.h"
#include "../engine/StockPlugins.h"
#include "../patchbay/PatchBay.h"
#include "../tracker/TrackerEngine.h"

#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QStatusBar>
#include <QtConcurrent>

#include <signal.h>

// The Retro Sampler (Ami) as the patchbay plays it; its slot N listens on MIDI channel N.
static constexpr const char* retroSampler = "retroSampler";

// The Tracker's linked instruments play the Retro Sampler in the patchbay. Their notes
// are timed by the Tracker on the audio thread, counted from the start of the Tracker's
// render call; the patchbay renders after the Tracker in the same audio block, so the
// frames the Tracker has rendered since the patchbay last did are added. The Tracker's
// own sound goes through the Master Section and the patchbay's does not, so the notes
// are also delayed by its latency, and the two stay in step.
class TrackerLink final : public TrackerEngine::LinkSink
{
public:
    TrackerLink (patchbay::PatchBay& b, AudioEngine& e) : bay (b), engine (e) {}

    std::atomic<int> synth { -1 };   // the Retro Sampler's patchbay synth, or -1

    void linkedNote (int offset, int channel, int note, int velocity) override
    {
        const auto s = synth.load();
        if (s < 0)
            return;
        const auto status = (uint8_t) ((velocity > 0 ? 0x90 : 0x80) | ((channel - 1) & 0x0F));
        bay.scheduleMidi (s, base.load() + offset + engine.listeningLatency(), status, (uint8_t) note, (uint8_t) velocity);
    }

    // Audio thread.
    void trackerRendering() { base = sincePatch.load(); }
    void trackerRendered (int frames) { sincePatch += frames; }
    void patchRendered() { sincePatch = 0; }

private:
    patchbay::PatchBay& bay;
    AudioEngine& engine;
    std::atomic<int> base { 0 }, sincePatch { 0 };
};

// Plays the linked instruments' notes through another Retro Sampler, made from the live
// one's state, while the Tracker records the song.
namespace
{
    class LinkRecorder final : public TrackerEngine::LinkRenderer
    {
    public:
        explicit LinkRecorder (mixer::InstrumentPtr i) : ami (std::move (i)) {}

        void reset() override { ami->reset(); }

        void render (const std::vector<std::array<int, 4>>& notes, float* left, float* right, int frames) override
        {
            events.clear();
            for (const auto& n : notes)
                events.push_back ({ std::clamp (n[0], 0, frames - 1),
                                    (uint8_t) ((n[3] > 0 ? 0x90 : 0x80) | ((n[1] - 1) & 0x0F)), (uint8_t) n[2], (uint8_t) n[3] });
            ami->process (events.data(), (int) events.size(), left, right, frames);
        }

    private:
        mixer::InstrumentPtr ami;
        std::vector<mixer::NoteEvent> events;
    };
}

void MainWindow::registerTrackerCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Tracker");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    tracker = std::make_unique<TrackerEngine>();

    add ("tracker.window", tr ("Tracker&1 (MilkyTracker)"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_K) }, [this]
    {
        auto* w = trackerWindow();
        w->show();
        w->raise();
        w->activateWindow();
        w->patternView()->setFocus();
    })->setStatusTip (tr ("Tracker1, from MilkyTracker: play XM, MOD, IT and S3M modules and record them into the editor"));
    add ("tracker.furnace", tr ("Tracker&2 (Furnace)"), {}, [this] { openFurnace(); })
        ->setStatusTip (tr ("Tracker2, Furnace: the chiptune tracker for over 80 sound chips, in its own window and look, with an opacity bar"));
    add ("tracker.new", tr ("&New Song"), {}, [this] { trackerNew(); })
        ->setStatusTip (tr ("Start a new, empty XM song in the Tracker: 8 channels and one pattern of 64 rows"));
    add ("tracker.open", tr ("&Open Module..."), {}, [this] { openModule(); })
        ->setStatusTip (tr ("Open a tracker module (XM, MOD, IT, S3M, MTM, 669, FAR, ...) in the Tracker"));
    add ("tracker.save", tr ("Sa&ve Song"), {}, [this] { trackerSave (false); })
        ->setStatusTip (tr ("Save the Tracker's song as an XM module"));
    add ("tracker.saveAs", tr ("Save Song &As..."), {}, [this] { trackerSave (true); });
    add ("tracker.loadSample", tr ("&Load Sample..."), {}, [this] { trackerLoadSample (trackerWindow()->currentInstrument()); })
        ->setStatusTip (tr ("Load an audio file into the Tracker's current instrument"));
    add ("tracker.playSong", tr ("Play &Song"), {}, [this] { trackerPlay (false); });
    add ("tracker.playPattern", tr ("Play &Pattern"), {}, [this] { trackerPlay (true); });
    add ("tracker.stop", tr ("S&top Tracker"), {}, [this] { trackerStop(); });
    add ("tracker.recordStereo", tr ("&Record to Editor (Stereo)..."), {}, [this] { trackerRecord (false); })
        ->setStatusTip (tr ("Record the whole song, as the Tracker plays it, into a new stereo tab"));
    add ("tracker.recordMulti", tr ("Record to Editor (&Multi-Channel)..."), {}, [this] { trackerRecord (true); })
        ->setStatusTip (tr ("Record the whole song into a new tab with a stereo pair per mixer track (T1 to T8 on the channels)"));
    add ("tracker.resetTracks", tr ("Spread Channels over the &Tracks"), {}, [this]
    {
        tracker->resetTracks();
        if (trackerWin != nullptr)
        {
            trackerWin->patternView()->update();
            trackerWin->refresh();
        }
        statusBar()->showMessage (tr ("The module's channels are spread evenly over tracks 1 to %1").arg (std::min (tracker->numChannels(), (int) TrackerEngine::numTracks)), 4000);
    });
    add ("tracker.about", tr ("About the Trackers..."), {}, [this]
    {
        QMessageBox::about (this, tr ("About the Trackers"),
                            tr ("<p>Tracker1 is built on <b>MilkyTracker</b> (milkytracker.org, github.com/milkytracker/MilkyTracker): "
                                "its player library MilkyPlay plays the modules, and its pattern display and its module and pattern editors are followed here.</p>"
                                "<p>MilkyTracker is by Peter \"pailes\" Barth and its contributors (Christopher O'Neill, Dale Whinham, "
                                "Julian Harnath and many others). MilkyPlay is under the New BSD licence and MilkyTracker under the "
                                "GNU GPL version 3 or later; PHI uses both with thanks, under the GNU AGPL version 3.</p>"
                                "<p>Tracker2 is <b>Furnace</b> (tildearrow.org/furnace, github.com/tildearrow/furnace) by tildearrow "
                                "and its contributors, under the GNU GPL version 2 or later. It runs as its own program, built by PHI "
                                "from its source with an opacity bar added along the bottom of its window.</p>"));
    });
}

// Tracker2 is Furnace, run as a program of its own in its own window: built from
// source into the stock folder (cmake/Trackers.cmake), with an opacity bar added along
// the bottom that it keeps in its own settings. It plays through the desktop's sound
// (PipeWire, JACK or ALSA), not through PHI, and stays open if PHI closes.
void MainWindow::openFurnace()
{
    if (furnacePid > 0 && ::kill ((pid_t) furnacePid, 0) == 0)
    {
        statusBar()->showMessage (tr ("Tracker2 (Furnace) is already open"), 4000);
        return;
    }
    const auto stockFolder = stock::folder();
    const auto program = stockFolder.isDirectory()
        ? QString::fromStdString (stockFolder.getChildFile ("Furnace/bin/furnace").getFullPathName().toStdString())
        : QString();
    if (program.isEmpty() || ! QFileInfo (program).isExecutable())
    {
        QMessageBox::warning (this, tr ("Tracker2 (Furnace)"),
                              tr ("Furnace isn't installed with this PHI. It is built with the stock instruments "
                                  "(PHI_STOCK_INSTRUMENTS), which need SDL2's build tools."));
        return;
    }
    QProcess process;
    process.setProgram (program);
    auto environment = QProcessEnvironment::systemEnvironment();
    // On X11 (XWayland under Wayland), where a window's opacity can be set, like the
    // instruments' windows, and with PHI's window class for window manager rules.
    environment.insert (QStringLiteral ("SDL_VIDEODRIVER"), QStringLiteral ("x11"));
    environment.insert (QStringLiteral ("SDL_VIDEO_X11_WMCLASS"), QStringLiteral ("phi"));
    process.setProcessEnvironment (environment);
    process.setStandardOutputFile (QProcess::nullDevice());
    process.setStandardErrorFile (QProcess::nullDevice());
    if (! process.startDetached (&furnacePid))
        QMessageBox::warning (this, tr ("Tracker2 (Furnace)"), tr ("Furnace could not be started: %1").arg (process.errorString()));
}

TrackerWindow* MainWindow::trackerWindow()
{
    if (trackerWin == nullptr)
    {
        trackerWin = new TrackerWindow (*tracker, this);
        connect (trackerWin, &TrackerWindow::newRequested, this, [this] { trackerNew(); });
        connect (trackerWin, &TrackerWindow::openRequested, this, [this] { openModule(); });
        connect (trackerWin, &TrackerWindow::saveRequested, this, [this] (bool saveAs) { trackerSave (saveAs); });
        connect (trackerWin, &TrackerWindow::loadSampleRequested, this, [this] (int instrument) { trackerLoadSample (instrument); });
        connect (trackerWin, &TrackerWindow::notePreview, this, [this] (int channel, int note, int instrument) { trackerPreview (channel, note, instrument); });
        connect (trackerWin, &TrackerWindow::noteRelease, this, [this] (int channel, int instrument) { tracker->releaseNote (channel, instrument); });
        connect (trackerWin, &TrackerWindow::playSongRequested, this, [this] { trackerPlay (false); });
        connect (trackerWin, &TrackerWindow::playPatternRequested, this, [this] { trackerPlay (true); });
        connect (trackerWin, &TrackerWindow::stopRequested, this, [this] { trackerStop(); });
        connect (trackerWin, &TrackerWindow::recordRequested, this, [this] (bool multi) { trackerRecord (multi); });
        connect (trackerWin, &TrackerWindow::linkRequested, this, [this] (int instrument, int slot) { trackerLink (instrument, slot); });
        connect (trackerWin, &TrackerWindow::retroSamplerRequested, this, [this] { openInstrument (retroSampler); });
        connect (trackerWin, &TrackerWindow::closed, this, [this]
        {
            trackerStop();
            activateWindow();
            waveform->setFocus();
        });
    }
    return trackerWin;
}

void MainWindow::openModule()
{
    auto* w = trackerWindow();
    const auto folder = appSettings().value (QStringLiteral ("tracker/folder"), QDir::homePath()).toString();
    const auto path = QFileDialog::getOpenFileName (w, tr ("Open Module"), folder,
                                                    tr ("Modules (*.xm *.mod *.it *.s3m *.stm *.mtm *.669 *.far *.mdl *.okt *.ptm *.ult *.amf *.dbm *.dsm *.gdm *.imf *.psm *.mxm *.umx);;All files (*)"));
    if (path.isEmpty() || ! trackerConfirmDiscard())
        return;
    appSettings().setValue (QStringLiteral ("tracker/folder"), QFileInfo (path).absolutePath());

    trackerStop();
    const auto error = tracker->load (path.toStdString());
    if (! error.empty())
    {
        QMessageBox::warning (w, tr ("Open Module"), QString::fromStdString (error));
        return;
    }
    trackerLoadLinks (path);
    w->moduleChanged();
    w->show();
    w->raise();
    w->activateWindow();
    w->patternView()->setFocus();
}

void MainWindow::trackerPlay (bool patternOnly)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
    {
        openModule();
        if (! tracker->isLoaded())
            return;
    }
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    auto* view = w->patternView();
    if (patternOnly)
        tracker->playPattern (tracker->patternAt (view->order()), view->row(), rate);
    else
        tracker->playSong (view->order(), view->row(), rate);
    trackerLiveSource();
    w->refresh();
}

void MainWindow::trackerStop()
{
    if (engine.hasLiveSource())
        engine.setLiveSource ({});
    tracker->stop();
    if (trackerWin != nullptr)
        trackerWin->refresh();
}

void MainWindow::trackerRecord (bool multiChannel)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
    {
        QMessageBox::information (w, tr ("Record to Editor"), tr ("Open a module first (Ctrl+O in the Tracker)."));
        return;
    }
    trackerStop();

    // The file's own rate would be 44.1 or 48 kHz anyway; use the device's so it sounds as heard.
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);

    QProgressDialog dialog (multiChannel ? tr ("Recording the song, a stereo pair per track...") : tr ("Recording the song..."),
                            tr ("Cancel"), 0, 1000, w);
    dialog.setWindowTitle (tr ("Record to Editor"));
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
    // The linked instruments are recorded through a copy of the Retro Sampler as it is now.
    std::shared_ptr<LinkRecorder> linked;
    if (tracker->hasLinks())
    {
        QString why;
        if (const auto s = trackerAmi (false, &why); s >= 0)
        {
            std::string error;
            const auto live = patchBay->synth (s);
            if (auto copy = engine.loadInstrumentPlugin (AudioEngine::instrumentPluginId (*live), rate, error))
            {
                copy->setState (live->state());
                copy->prepare (rate, 4096);
                linked = std::make_shared<LinkRecorder> (std::move (copy));
            }
            else
                why = QString::fromStdString (error);
        }
        if (linked == nullptr
            && QMessageBox::question (w, tr ("Record to Editor"),
                                      tr ("The instruments linked to the Retro Sampler can't be recorded (%1). Record the song without them?")
                                          .arg (why.isEmpty() ? tr ("it is not open") : why))
                   != QMessageBox::Yes)
            return;
    }

    watcher.setFuture (QtConcurrent::run ([t = tracker.get(), rate, multiChannel, progress, cancel, linked]
    {
        Result r;
        r.data = t->record (rate, multiChannel, r.error, progress.get(), cancel.get(), linked.get());
        return r;
    }));
    loop.exec();
    poll.stop();
    dialog.reset();

    auto result = watcher.result();
    if (result.data == nullptr || result.data->length() == 0)
    {
        if (result.error != "Cancelled.")
            QMessageBox::warning (w, tr ("Record to Editor"), result.error.empty() ? tr ("The song made no sound.") : QString::fromStdString (result.error));
        return;
    }

    Tags tags;
    if (const auto name = tracker->title(); ! name.empty())
        tags[tag::title] = name;
    const auto options = saveOptions;
    newTab();
    doc.reset (std::move (result.data), {}, {}, std::move (tags));
    doc.markUnsaved();
    saveOptions = options;
    refreshFromDocument (false);
    activateWindow();
    raise();
    waveform->setFocus();
    const auto channels = data() != nullptr ? data()->numChannels() : 2;
    statusBar()->showMessage (multiChannel ? tr ("Recorded the song into a new tab: %1 channels, a stereo pair per track").arg (channels)
                                           : tr ("Recorded the song into a new stereo tab"), 8000);
}

bool MainWindow::trackerConfirmDiscard()
{
    if (tracker == nullptr || ! tracker->isModified())
        return true;
    auto* w = trackerWindow();
    w->show();
    w->raise();
    auto name = QString::fromStdString (tracker->title());
    if (name.isEmpty() && ! tracker->path().empty())
        name = QFileInfo (QString::fromStdString (tracker->path())).fileName();
    name = name.isEmpty() ? tr ("the Tracker song") : QStringLiteral ("\u201c%1\u201d").arg (name);
    const auto answer = QMessageBox::question (w, tr ("Tracker"), tr ("Save the changes to %1?").arg (name),
                                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return trackerSave (false);
    return answer == QMessageBox::Discard;
}

void MainWindow::trackerNew()
{
    auto* w = trackerWindow();
    if (! trackerConfirmDiscard())
        return;
    trackerStop();
    tracker->newSong (8);
    w->moduleChanged();
    w->show();
    w->raise();
    w->activateWindow();
    w->patternView()->setFocus();
    w->patternView()->setEditing (true);
}

bool MainWindow::trackerSave (bool saveAs)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
        return false;
    auto path = QString::fromStdString (tracker->path());
    if (saveAs || path.isEmpty() || ! path.endsWith (QStringLiteral (".xm"), Qt::CaseInsensitive))
    {
        if (! tracker->isEditable()
            && QMessageBox::question (w, tr ("Save Song"), tr ("The song is saved as an XM module (FastTracker 2), which PHI can edit. Continue?")) != QMessageBox::Yes)
            return false;
        const auto folder = appSettings().value (QStringLiteral ("tracker/folder"), QDir::homePath()).toString();
        auto base = tracker->title().empty() ? tr ("Untitled") : QString::fromStdString (tracker->title());
        if (! path.isEmpty())
            base = QFileInfo (path).completeBaseName();
        path = QFileDialog::getSaveFileName (w, tr ("Save Song As"), QDir (folder).filePath (base + QStringLiteral (".xm")), tr ("XM modules (*.xm)"));
        if (path.isEmpty())
            return false;
        if (! path.endsWith (QStringLiteral (".xm"), Qt::CaseInsensitive))
            path += QStringLiteral (".xm");
        appSettings().setValue (QStringLiteral ("tracker/folder"), QFileInfo (path).absolutePath());
    }
    const auto error = tracker->save (path.toStdString());
    if (! error.empty())
    {
        QMessageBox::warning (w, tr ("Save Song"), QString::fromStdString (error));
        return false;
    }
    trackerSaveLinks (path);
    w->songChanged();
    statusBar()->showMessage (tr ("Saved the song to %1").arg (path), 5000);
    return true;
}

void MainWindow::trackerLoadSample (int instrument)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
        trackerNew();
    if (! tracker->isLoaded())
        return;
    const auto folder = appSettings().value (QStringLiteral ("tracker/sampleFolder"),
                                             appSettings().value (QStringLiteral ("tracker/folder"), QDir::homePath())).toString();
    QStringList patterns;
    for (const auto& ext : AudioEngine::readableExtensions())
        patterns << QStringLiteral ("*.") + QString::fromStdString (ext);
    const auto path = QFileDialog::getOpenFileName (w, tr ("Load Sample into Instrument %1").arg (instrument), folder,
                                                    tr ("Audio files (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
    if (path.isEmpty())
        return;
    appSettings().setValue (QStringLiteral ("tracker/sampleFolder"), QFileInfo (path).absolutePath());

    std::vector<Marker> markers;
    Tags tags;
    std::string error;
    const auto audio = AudioEngine::loadFile (path.toStdString(), markers, tags, error);
    if (audio == nullptr)
    {
        QMessageBox::warning (w, tr ("Load Sample"), error.empty() ? tr ("The file could not be read.") : QString::fromStdString (error));
        return;
    }
    error = tracker->loadSample (instrument, *audio, QFileInfo (path).completeBaseName().toStdString());
    if (! error.empty())
    {
        QMessageBox::warning (w, tr ("Load Sample"), QString::fromStdString (error));
        return;
    }
    w->songChanged();
    w->patternView()->setFocus();
    statusBar()->showMessage (tr ("Loaded %1 into instrument %2").arg (QFileInfo (path).fileName()).arg (instrument), 5000);
}

void MainWindow::trackerPreview (int channel, int note, int instrument)
{
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    if (tracker->link (instrument) > 0)
        trackerAmi (false);   // where the note goes
    tracker->previewNote (channel, note, instrument, rate);
    if (! engine.hasLiveSource())
        trackerLiveSource();
}

//==============================================================================
// Linked instruments

void MainWindow::trackerConnectLinks()
{
    trackerLinks = std::make_shared<TrackerLink> (*patchBay, engine);
    tracker->setLinkSink (trackerLinks.get());
}

void MainWindow::trackerPatchRendered()
{
    if (trackerLinks != nullptr)
        trackerLinks->patchRendered();
}

// The Tracker as the engine's live source, counting what it renders for its linked notes.
void MainWindow::trackerLiveSource()
{
    if (tracker->hasLinks())
        trackerAmi (false);   // the patchbay plays the Retro Sampler
    engine.setLiveSource ([t = tracker.get(), l = trackerLinks.get()] (float* left, float* right, int frames)
    {
        l->trackerRendering();
        t->render (left, right, frames);
        l->trackerRendered (frames);
    });
}

int MainWindow::trackerAmi (bool load, QString* why)
{
    const auto s = instrumentSynth (retroSampler, load, why);
    trackerLinks->synth = s;
    if (s >= 0)
        playInstruments();
    return s;
}

// Links an instrument to a Retro Sampler slot (0: its own sample again). Each slot is
// set to listen on its own MIDI channel, so slots never play each other's notes.
void MainWindow::trackerLink (int instrument, int slot)
{
    auto* w = trackerWindow();
    if (slot > 0)
    {
        QString why;
        const auto s = trackerAmi (true, &why);
        if (s < 0)
        {
            QMessageBox::warning (w, tr ("Retro Sampler"), why);
            return;
        }
        // Every slot on its own channel: a slot left on Omni (its default) would play the
        // other slots' notes too.
        for (int n = 1; n <= TrackerEngine::numSlots; ++n)
            patchBay->synth (s)->setParameter ("Sample MIDI Chan" + std::to_string (n - 1), (float) n / 16.0f);
    }
    if (const auto error = tracker->setLink (instrument, slot); ! error.empty())
    {
        QMessageBox::warning (w, tr ("Retro Sampler"), QString::fromStdString (error));
        return;
    }
    w->songChanged();
    statusBar()->showMessage (slot > 0 ? tr ("Instrument %1 plays through slot %2 of the Retro Sampler (MIDI channel %2)").arg (instrument).arg (slot)
                                       : tr ("Instrument %1 plays its own sample again").arg (instrument), 6000);
}

// The links, and the Retro Sampler's state, are kept next to the song as song.xm.ami
// (XM has no place for them): a line "link <instrument> <slot>" per linked instrument
// and "state <base64>", the sampler's own saved state with its samples.
void MainWindow::trackerSaveLinks (const QString& path)
{
    const auto side = path + QStringLiteral (".ami");
    const auto links = tracker->links();
    if (links.empty())
    {
        QFile::remove (side);   // made by us for this song: its links are gone
        return;
    }
    QFile file (side);
    if (! file.open (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        statusBar()->showMessage (tr ("The Retro Sampler links could not be saved to %1").arg (side), 8000);
        return;
    }
    QByteArray text ("PHINYX Tracker links 1\n");   // the file's header from before the rename to PHI, kept as it is
    for (const auto& [instrument, slot] : links)
        text += QStringLiteral ("link %1 %2\n").arg (instrument).arg (slot).toUtf8();
    if (const auto s = trackerAmi (false); s >= 0)
        text += "state " + QByteArray::fromStdString (patchBay->synth (s)->state()) + "\n";
    file.write (text);
}

void MainWindow::trackerLoadLinks (const QString& path)
{
    QFile file (path + QStringLiteral (".ami"));
    if (! file.open (QIODevice::ReadOnly | QIODevice::Text))
        return;
    std::vector<std::pair<int, int>> links;
    std::string state;
    while (! file.atEnd())
    {
        const auto line = file.readLine().trimmed();
        const auto words = line.split (' ');
        if (words.size() == 3 && words[0] == "link")
            links.emplace_back (words[1].toInt(), words[2].toInt());
        else if (words.size() == 2 && words[0] == "state")
            state = words[1].toStdString();
    }
    if (links.empty())
        return;
    QString why;
    const auto s = trackerAmi (true, &why);
    if (s < 0)
    {
        statusBar()->showMessage (tr ("The song's Retro Sampler instruments can't play: %1").arg (why), 8000);
        return;
    }
    if (! state.empty())
        patchBay->synth (s)->setState (state);
    for (const auto& [instrument, slot] : links)
        tracker->setLink (instrument, slot);
}
