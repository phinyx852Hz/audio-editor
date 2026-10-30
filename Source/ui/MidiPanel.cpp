// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiPanel.h"
#include "MidiEditor.h"
#include "Settings.h"
#include "../engine/AudioEngine.h"
#include "../mixer/Instruments.h"
#include "../mixer/MidiFile.h"
#include "../patchbay/PatchBay.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    // A new song: one track, 120 BPM, no notes.
    midifile::Song emptySong()
    {
        midifile::Song s;
        s.format = 0;
        s.ticksPerQuarter = 480;
        s.tempos.push_back ({ 0, 500000.0 });
        return s;
    }

    QString folder()
    {
        return appSettings().value (QStringLiteral ("midiEditor/folder")).toString();
    }
}

MidiPanel::MidiPanel (Host h, QWidget* parent) : QFrame (parent), host (std::move (h))
{
    setObjectName (QStringLiteral ("midiPanel"));
    setFrameShape (QFrame::NoFrame);
    setAccessibleName (tr ("MIDI editor"));
    layout = new QVBoxLayout (this);
    layout->setContentsMargins (4, 4, 4, 2);
    layout->setSpacing (3);

    // The bar: the instrument, the file, the bounce, and the close.
    auto* bar = new QHBoxLayout;
    bar->setSpacing (4);
    auto* title = new QLabel (tr ("MIDI"), this);
    auto bold = title->font();
    bold.setBold (true);
    title->setFont (bold);
    bar->addWidget (title);
    nameLabel = new QLabel (this);
    nameLabel->setForegroundRole (QPalette::PlaceholderText);
    bar->addWidget (nameLabel);
    bar->addSpacing (8);
    instrumentButton = new QPushButton (this);
    instrumentButton->setFocusPolicy (Qt::NoFocus);
    instrumentButton->setToolTip (tr ("What the notes and the MIDI keyboard play: the built-in Synth or an instrument plugin, the stock ones included"));
    connect (instrumentButton, &QPushButton::clicked, this, [this] { instrumentMenu(); });
    bar->addWidget (instrumentButton);
    editButton = new QPushButton (tr ("Edit"), this);
    editButton->setFocusPolicy (Qt::NoFocus);
    editButton->setToolTip (tr ("The instrument's own window"));
    connect (editButton, &QPushButton::clicked, this, [this]
    {
        if (ensureInstrument() && instrument->hasWindow())
            instrument->openWindow();
    });
    bar->addWidget (editButton);
    bar->addStretch (1);
    const auto button = [this, bar] (const QString& text, const QString& tip, auto fn)
    {
        auto* b = new QPushButton (text, this);
        b->setFocusPolicy (Qt::NoFocus);
        b->setToolTip (tip);
        connect (b, &QPushButton::clicked, this, fn);
        bar->addWidget (b);
        return b;
    };
    button (tr ("New"), tr ("An empty MIDI editor"), [this] { newSong(); });
    button (tr ("Open..."), tr ("A MIDI file (.mid) into the MIDI editor"), [this] { openSong(); });
    button (tr ("Save..."), tr ("The notes as a MIDI file (.mid)"), [this] { saveSong(); });
    bar->addSpacing (6);
    button (tr ("Bounce to Audio"), tr ("The notes through the instrument, with its tail, into a new tab of the audio editor"), [this] { bounce(); });
    auto* close = new QToolButton (this);
    close->setText (QStringLiteral ("✕"));
    close->setAutoRaise (true);
    close->setFocusPolicy (Qt::NoFocus);
    close->setAccessibleName (tr ("Close the MIDI editor"));
    close->setToolTip (tr ("Close the MIDI editor (Shift+F4, or View > MIDI Editor, opens it again)"));
    connect (close, &QToolButton::clicked, this, [this] { if (host.closeRequested) host.closeRequested(); });
    bar->addWidget (close);
    layout->addLayout (bar);

    instrumentId = appSettings().value (QStringLiteral ("midiEditor/instrument")).toString();
    load (emptySong(), {});
    refreshBar();
}

MidiPanel::~MidiPanel()
{
    // The patchbay synth it played as is let go.
    if (host.patchBay != nullptr && slot >= 0 && instrument != nullptr && host.patchBay->synth (slot) == instrument)
        host.patchBay->setSynth (slot, nullptr);
}

void MidiPanel::showEvent (QShowEvent* e)
{
    QFrame::showEvent (e);
    ensureInstrument();
}

void MidiPanel::midiIn (uint8_t status, uint8_t data1, uint8_t data2)
{
    if (editor != nullptr)
        editor->midiIn (status, data1, data2);
}

std::vector<uint8_t> MidiPanel::bytesNow() const
{
    if (editor == nullptr)
        return {};
    const auto song = editor->result();
    return song != nullptr ? midifile::bytes (*song) : std::vector<uint8_t> {};
}

bool MidiPanel::mayDiscard()
{
    if (bytesNow() == saved)
        return true;
    const auto answer = QMessageBox::question (this, tr ("MIDI Editor"), tr ("The MIDI editor's notes are not saved. Save them first?"),
                                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
    {
        saveSong();
        return bytesNow() == saved;
    }
    return true;
}

void MidiPanel::load (const midifile::Song& song, const QString& songName)
{
    MidiEditor::Host h;
    h.embedded = true;
    h.title = songName;
    h.folder = folder();
    h.play = [this] (uint8_t s, uint8_t a, uint8_t b)
    {
        if (host.patchBay != nullptr && slot >= 0 && instrument != nullptr && host.patchBay->synth (slot) == instrument)
            host.patchBay->liveMidi (slot, s, a, b);
    };
    h.loadAudio = [] (const QString& file, QString& why) -> AudioDataPtr
    {
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
        auto loaded = AudioEngine::loadFile (file.toStdString(), markers, tags, error);
        why = QString::fromStdString (error);
        return loaded;
    };
    if (editor != nullptr)
        delete editor.data();
    editor = new MidiEditor (song, h, this);
    layout->addWidget (editor, 1);
    editor->show();
    name = songName;
    saved = bytesNow();
    refreshBar();
}

void MidiPanel::newSong()
{
    if (! mayDiscard())
        return;
    path.clear();
    load (emptySong(), {});
}

void MidiPanel::openSong()
{
    if (! mayDiscard())
        return;
    const auto file = QFileDialog::getOpenFileName (this, tr ("Open MIDI File"), folder(), tr ("MIDI files (*.mid *.midi *.smf);;All files (*)"));
    if (file.isEmpty())
        return;
    appSettings().setValue (QStringLiteral ("midiEditor/folder"), QFileInfo (file).absolutePath());
    std::string error;
    const auto song = midifile::read (file.toStdString(), error);
    if (song == nullptr)
    {
        QMessageBox::warning (this, tr ("Open MIDI File"), QString::fromStdString (error));
        return;
    }
    if (song->ticksPerQuarter <= 0)
    {
        QMessageBox::warning (this, tr ("Open MIDI File"), tr ("This MIDI file counts in SMPTE time, so it cannot be edited here."));
        return;
    }
    path = file;
    load (*song, QFileInfo (file).completeBaseName());
}

void MidiPanel::saveSong()
{
    if (editor == nullptr)
        return;
    auto file = QFileDialog::getSaveFileName (this, tr ("Save MIDI File"), path.isEmpty() ? folder() : path, tr ("MIDI files (*.mid)"));
    if (file.isEmpty())
        return;
    if (QFileInfo (file).suffix().isEmpty())
        file += QStringLiteral (".mid");
    appSettings().setValue (QStringLiteral ("midiEditor/folder"), QFileInfo (file).absolutePath());
    std::string error;
    const auto song = editor->result();
    if (song == nullptr || ! midifile::write (*song, file.toStdString(), error))
    {
        QMessageBox::warning (this, tr ("Save MIDI File"), QString::fromStdString (error));
        return;
    }
    path = file;
    name = QFileInfo (file).completeBaseName();
    saved = midifile::bytes (*song);
    refreshBar();
    if (host.say)
        host.say (tr ("Saved %1").arg (QFileInfo (file).fileName()));
}

void MidiPanel::refreshBar()
{
    nameLabel->setText (name.isEmpty() ? tr ("untitled") : name);
    const auto text = instrument != nullptr ? QString::fromStdString (instrument->name()) : instrumentId.isEmpty() ? tr ("Synth") : tr ("Instrument");
    instrumentButton->setText (text + QStringLiteral (" ▾"));
    editButton->setEnabled (instrument != nullptr && instrument->hasWindow());
}

void MidiPanel::instrumentMenu()
{
    if (host.engine == nullptr)
        return;
    QMenu menu (this);
    auto* synth = menu.addAction (tr ("&Synth (built in)"), this, [this] { choose ({}); });
    synth->setCheckable (true);
    synth->setChecked (instrumentId.isEmpty());
    const auto plugins = host.engine->knownInstruments();
    if (! plugins.empty())
        menu.addSeparator();
    for (const auto& p : plugins)
    {
        if (! p.enabled)
            continue;
        const auto id = QString::fromStdString (p.id);
        auto* a = menu.addAction (QString::fromStdString (p.name) + QStringLiteral (" (") + QString::fromStdString (p.format) + QLatin1Char (')'),
                                  this, [this, id] { choose (id); });
        a->setCheckable (true);
        a->setChecked (id == instrumentId);
    }
    menu.exec (instrumentButton->mapToGlobal (QPoint (0, instrumentButton->height())));
}

bool MidiPanel::ensureInstrument()
{
    if (instrument != nullptr)
        return place (instrument);   // back in its patchbay slot, if that was taken meanwhile
    return choose (instrumentId) || (! instrumentId.isEmpty() && choose ({}));   // a plugin gone: the Synth
}

bool MidiPanel::choose (const QString& pluginId)
{
    if (host.patchBay == nullptr || host.engine == nullptr)
        return false;
    if (host.heard)
        host.heard();   // the patchbay prepared at the device's rate
    std::shared_ptr<mixer::Instrument> made;
    if (pluginId.isEmpty())
        made = std::make_shared<mixer::Synth>();
    else
    {
        std::string error;
        QApplication::setOverrideCursor (Qt::WaitCursor);
        made = host.engine->loadInstrumentPlugin (pluginId.toStdString(), host.patchBay->sampleRate(), error);
        QApplication::restoreOverrideCursor();
        if (made == nullptr)
        {
            if (host.say)
                host.say (tr ("The instrument could not be started: %1").arg (QString::fromStdString (error)));
            return false;
        }
    }
    if (! place (made))
        return false;
    instrumentId = pluginId;
    appSettings().setValue (QStringLiteral ("midiEditor/instrument"), instrumentId);
    refreshBar();
    return true;
}

bool MidiPanel::place (std::shared_ptr<mixer::Instrument> made)
{
    auto& bay = *host.patchBay;
    if (! (slot >= 0 && instrument != nullptr && bay.synth (slot) == instrument))
    {
        // Its slot was given to something else (or it never had one): the first free synth.
        slot = -1;
        for (int i = 0; i < patchbay::maxSynths && slot < 0; ++i)
            if (bay.synth (i) == nullptr)
                slot = i;
        if (slot < 0)
        {
            if (host.say)
                host.say (tr ("All %1 patchbay synths are in use: clear one in the Patchbay (F4) for the MIDI editor").arg (patchbay::maxSynths));
            instrument = nullptr;
            refreshBar();
            return false;
        }
    }
    if (bay.synth (slot) != made)
        bay.setSynth (slot, made);
    instrument = std::move (made);
    if (host.keyboard)
        host.keyboard (slot);
    refreshBar();
    return true;
}

// The notes through a copy of the instrument (its settings as they are now), from the start to
// the end of the last note, then on until its tail dies away (up to 10 seconds).
void MidiPanel::bounce()
{
    if (editor == nullptr || ! ensureInstrument())
        return;
    const auto song = editor->result();
    if (song == nullptr || song->noteCount() == 0)
    {
        if (host.say)
            host.say (tr ("No notes to bounce"));
        return;
    }
    const auto rate = host.patchBay->sampleRate() > 0.0 ? host.patchBay->sampleRate() : 48000.0;
    std::shared_ptr<mixer::Instrument> copy;
    if (instrumentId.isEmpty())
        copy = std::make_shared<mixer::Synth>();
    else
    {
        std::string error;
        QApplication::setOverrideCursor (Qt::WaitCursor);
        copy = host.engine->loadInstrumentPlugin (instrumentId.toStdString(), rate, error);
        QApplication::restoreOverrideCursor();
        if (copy == nullptr)
        {
            QMessageBox::warning (this, tr ("Bounce to Audio"), tr ("The instrument could not be started: %1").arg (QString::fromStdString (error)));
            return;
        }
    }
    constexpr int block = 256;
    copy->setState (instrument->state());
    copy->prepare (rate, block);
    copy->reset();

    struct Timed { Frame at; mixer::NoteEvent event; };
    std::vector<Timed> events;
    events.reserve (song->events.size());
    for (const auto& e : song->events)
        events.push_back ({ (Frame) std::llround (song->seconds (e.tick) * rate), { 0, e.status, e.data1, e.data2 } });
    std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b) { return a.at < b.at; });
    const auto end = std::max<Frame> (events.empty() ? 0 : events.back().at, (Frame) std::llround (song->lengthSeconds() * rate));
    const auto longest = end + (Frame) (10.0 * rate);
    const auto quietNeeded = (Frame) (0.25 * rate);

    QApplication::setOverrideCursor (Qt::WaitCursor);
    AudioData::Builder out (2, rate);
    std::vector<float> left (block), right (block);
    std::vector<mixer::NoteEvent> now;
    size_t next = 0;
    Frame pos = 0, quiet = 0;
    bool released = false;
    while (pos < longest)
    {
        const auto frames = (int) std::min<Frame> (block, longest - pos);
        now.clear();
        while (next < events.size() && events[next].at < pos + frames)
        {
            auto e = events[next++].event;
            e.offset = (int) std::max<Frame> (0, events[next - 1].at - pos);
            now.push_back (e);
        }
        if (! released && pos + frames > end)
        {
            for (uint8_t c = 0; c < 16; ++c)
                now.push_back ({ frames - 1, (uint8_t) (0xB0 | c), 123, 0 });   // all notes off: the tails ring on
            released = true;
        }
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        copy->process (now.data(), (int) now.size(), left.data(), right.data(), frames);
        const float* channels[2] { left.data(), right.data() };
        out.append (channels, frames);
        pos += frames;
        if (pos > end)
        {
            float peak = 0.0f;
            for (int i = 0; i < frames; ++i)
                peak = std::max ({ peak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]) });
            quiet = peak < 1.0e-4f ? quiet + frames : 0;   // -80 dB
            if (quiet >= quietNeeded)
                break;
        }
    }
    auto data = out.finish();
    QApplication::restoreOverrideCursor();
    if (host.bounced)
        host.bounced (std::move (data), name.isEmpty() ? tr ("MIDI bounce") : name);
}
