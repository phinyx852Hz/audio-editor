// SPDX-License-Identifier: AGPL-3.0-only
// The mixer as control surfaces and MIDI Learn see it (docs/control-surfaces.md): what lights
// their pads and buttons, and what their pads, knobs and buttons do to the mix.
#include "MixerWindow.h"
#include "../control/ControlSurface.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QSlider>

#include <algorithm>
#include <cmath>

control::State MixerWindow::surfaceState() const
{
    control::State s;
    s.tracks = std::min (engine.numTracks(), control::maxTracks);
    s.scenes = numScenes;
    s.selected = selected < mixer::maxTracks ? selected : -1;
    for (int t = 0; t < s.tracks; ++t)
    {
        auto& out = s.track[(size_t) t];
        const auto& strip = engine.strip (t);
        const auto launch = engine.launchInfo (t);
        const auto rec = engine.recordInfo (t);
        out.armed = engine.liveTrack() == t;
        out.mute = strip.mute.load();
        out.solo = strip.solo.load();
        out.playing = launch.playing >= 0;
        out.rgb = info[(size_t) t].colour.rgb() & 0xFFFFFF;
        out.volume = control::unitFromDb (strip.faderDb.load());
        out.pan = strip.pan.load();
        out.name = trackName (t).toStdString();
        if (launch.playing >= 0 && launch.playing < numScenes)
            out.clip = slotName (t, launch.playing).toStdString();
        for (size_t k = 0; k < out.send.size(); ++k)
            out.send[k] = control::unitFromDb (strip.sendDb[k].load());
        for (int sc = 0; sc < control::maxScenes && sc < mixer::maxScenes; ++sc)
        {
            auto& slot = s.slot[(size_t) t][(size_t) sc];
            slot.clip = slots[(size_t) t][(size_t) sc] != nullptr;
            slot.playing = launch.playing == sc;
            slot.queued = launch.queued == sc;
            if (rec.state != mixer::RecordState::none && rec.scene == sc)
            {
                slot.recording = true;
                slot.queued = rec.state == mixer::RecordState::waiting;
            }
        }
    }
    s.playing = engine.isPlaying() || engine.countingIn();
    s.recording = engine.isRecording();
    s.metronome = engine.metronome.load();
    s.loop = loopButton != nullptr && loopButton->isChecked();
    s.master = control::unitFromDb (engine.masterDb.load());
    s.tempo = tempoBox->value();
    return s;
}

void MixerWindow::surfaceCommand (const control::Command& c)
{
    using A = control::Action;
    const int tracks = engine.numTracks();
    // -1: the selected track (none while the master is selected).
    const int track = c.track >= 0 ? c.track : selected < mixer::maxTracks ? selected : -1;
    const bool trackOk = track >= 0 && track < tracks;
    auto flip = [&c] (bool now) { return c.value < 0.0f ? ! now : c.value >= 0.5f; };
    auto strip = [this, track] () -> mixer::Strip& { return engine.strip (track); };
    switch (c.action)
    {
        case A::togglePlay: togglePlay(); break;
        case A::stop:
            if (engine.isPlaying() || engine.countingIn())
                engine.stop();
            else
                toStart();
            refreshTransport();
            break;
        case A::record: recordPressed(); break;
        case A::capture: captureMidi(); break;
        case A::metronome:
            if (clickButton != nullptr)
                clickButton->toggle();
            break;
        case A::loop: loopButton->toggle(); break;
        case A::undo: undo(); break;
        case A::redo: redo(); break;
        case A::tapTempo:
        {
            // The tempo of the last taps, once two come within two seconds of each other.
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (! taps.empty() && now - taps.back() > 2000)
                taps.clear();
            taps.push_back (now);
            if (taps.size() > 5)
                taps.erase (taps.begin());
            if (taps.size() >= 2)
                tempoBox->setValue (60000.0 * (double) (taps.size() - 1) / (double) (taps.back() - taps.front()));
            break;
        }
        case A::tempo: tempoBox->setValue (c.value); break;
        case A::tempoBy: tempoBox->setValue (tempoBox->value() + c.value); break;
        case A::launch:
        {
            if (! trackOk || c.scene < 0 || c.scene >= mixer::maxScenes)
                break;
            const auto rec = engine.recordInfo (track);
            if (rec.state != mixer::RecordState::none && rec.scene == c.scene)
            {
                // The slot recording: Record's next step for it (stop, or end the overdub).
                if (engine.liveTrack() != track)
                    arm (track);
                recordPressed();
            }
            else if (c.scene < numScenes && slots[(size_t) track][(size_t) c.scene] != nullptr)
                launchSlot (track, c.scene);
            else if (engine.liveTrack() == track)
                recordSlot (track, c.scene);
            else
                engine.stopTrack (track);
            refreshViews();
            break;
        }
        case A::launchScene:
            if (c.scene >= 0 && c.scene < numScenes)
            {
                const bool wasPlaying = engine.isPlaying();
                engine.launchScene (c.scene);
                if (! wasPlaying && engine.isPlaying())
                    Q_EMIT playStarted();
                cursorScene = c.scene;
                refreshViews();
            }
            break;
        case A::stopTrack:
            if (trackOk)
                engine.stopTrack (track);
            break;
        case A::stopAll: engine.stopAll(); break;
        case A::select:
            if (trackOk)
                select (track);
            break;
        case A::selectBy:
            if (tracks > 0)
                select (std::clamp ((selected < mixer::maxTracks ? selected : 0) + (int) c.value, 0, tracks - 1));
            break;
        case A::arm:
            if (trackOk && flip (engine.liveTrack() == track) != (engine.liveTrack() == track))
                arm (track);
            break;
        case A::mute:
            if (trackOk)
            {
                strip().mute = flip (strip().mute.load());
                stripsChanged();
            }
            break;
        case A::solo:
            if (trackOk)
            {
                strip().solo = flip (strip().solo.load());
                stripsChanged();
            }
            break;
        case A::volume:
        case A::volumeBy:
            if (trackOk)
            {
                const float unit = c.action == A::volume ? c.value : control::unitFromDb (strip().faderDb.load()) + c.value;
                strip().faderDb = control::dbFromUnit (std::clamp (unit, 0.0f, 1.0f));
                stripsChanged();
            }
            break;
        case A::pan:
        case A::panBy:
            if (trackOk)
            {
                strip().pan = std::clamp (c.action == A::pan ? c.value : strip().pan.load() + c.value, -1.0f, 1.0f);
                stripsChanged();
            }
            break;
        case A::send:
        case A::sendBy:
            if (trackOk && c.index >= 0 && c.index < mixer::numReturns)
            {
                auto& send = strip().sendDb[(size_t) c.index];
                const float unit = c.action == A::send ? c.value : control::unitFromDb (send.load()) + c.value;
                send = control::dbFromUnit (std::clamp (unit, 0.0f, 1.0f));
                stripsChanged();
            }
            break;
        case A::master:
        case A::masterBy:
        {
            const float unit = c.action == A::master ? c.value : control::unitFromDb (engine.masterDb.load()) + c.value;
            masterFader->setValue ((int) std::lround (std::clamp (unit, 0.0f, 1.0f) * (float) masterFader->maximum()));
            break;
        }
        case A::showLauncher: showLauncher (true); break;
        case A::none: break;
    }
}
