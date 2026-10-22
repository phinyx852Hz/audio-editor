// SPDX-License-Identifier: AGPL-3.0-only
#include "MixerEngine.h"

#include <algorithm>
#include <cmath>

namespace mixer
{
    namespace
    {
        float dbToGain (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

        // Ramps a gain across the block so moving a control never clicks.
        void addRamped (float* out, const float* in, int frames, float from, float to)
        {
            if (std::abs (to - from) < 1.0e-9f)
            {
                if (std::abs (to) > 1.0e-9f)
                    for (int i = 0; i < frames; ++i)
                        out[i] += in[i] * to;
                return;
            }
            const auto step = (to - from) / (float) frames;
            for (int i = 0; i < frames; ++i)
                out[i] += in[i] * (from + step * (float) (i + 1));
        }
    }

    AudioClipPtr makeAudioClip (const AudioData& data, const std::string& path)
    {
        auto clip = std::make_shared<AudioClip>();
        clip->path = path;
        const auto n = (size_t) data.length();
        clip->left.resize (n);
        clip->right.resize (n);
        clip->mono = data.numChannels() < 2;
        float* dest[2] = { clip->left.data(), clip->right.data() };
        if (clip->mono)
        {
            float* one[1] = { clip->left.data() };
            data.read (0, (int) std::min<size_t> (n, (size_t) INT32_MAX), one);
            std::copy (clip->left.begin(), clip->left.end(), clip->right.begin());
        }
        else
        {
            // The first two channels; read in parts so a long file needs no third buffer.
            std::vector<std::vector<float>> scratch ((size_t) data.numChannels(), std::vector<float> (65536));
            std::vector<float*> ptrs;
            for (auto& c : scratch)
                ptrs.push_back (c.data());
            for (size_t at = 0; at < n; at += 65536)
            {
                const auto count = (int) std::min<size_t> (65536, n - at);
                data.read ((Frame) at, count, ptrs.data());
                for (int ch = 0; ch < 2; ++ch)
                    std::copy (scratch[(size_t) ch].begin(), scratch[(size_t) ch].begin() + count, dest[ch] + at);
            }
        }
        return clip;
    }

    MidiClipPtr placeMidi (midifile::SongPtr song, const std::string& path, double sampleRate, double bpm)
    {
        if (song == nullptr)
            return nullptr;
        auto clip = std::make_shared<MidiClip>();
        clip->song = song;
        clip->path = path;
        clip->bpm = bpm;
        clip->events.reserve (song->events.size());

        // Seconds walk the tempo map once rather than from the start for every event.
        const auto tpq = (double) std::max (1, song->ticksPerQuarter);
        size_t nextTempo = 0;
        int64_t fromTick = 0;
        double fromSeconds = 0.0, micros = 500000.0;
        auto secondsAt = [&] (int64_t tick)
        {
            if (song->ticksPerSecond > 0.0 || bpm > 0.0)
                return song->seconds (tick, bpm);
            while (nextTempo < song->tempos.size() && song->tempos[nextTempo].tick <= tick)
            {
                const auto& t = song->tempos[nextTempo++];
                fromSeconds += (double) (t.tick - fromTick) / tpq * micros * 1.0e-6;
                fromTick = t.tick;
                micros = t.microsPerQuarter;
            }
            return fromSeconds + (double) (tick - fromTick) / tpq * micros * 1.0e-6;
        };
        for (const auto& e : song->events)
            clip->events.push_back ({ (Frame) std::llround (secondsAt (e.tick) * sampleRate), e.status, e.data1, e.data2 });
        clip->length = (Frame) std::llround (std::max (song->lengthSeconds (bpm), 0.0) * sampleRate);
        if (! clip->events.empty())
            clip->length = std::max (clip->length, clip->events.back().frame);
        return clip;
    }

    MidiClipPtr reverseMidi (const MidiClip& in)
    {
        auto out = std::make_shared<MidiClip> (in);
        out->events.clear();
        out->events.reserve (in.events.size());
        const auto length = in.length;
        std::array<std::array<std::vector<size_t>, 128>, 16> open;
        auto mirror = [length] (Frame f) { return std::max<Frame> (0, length - f); };
        for (size_t i = 0; i < in.events.size(); ++i)
        {
            const auto& e = in.events[i];
            const auto type = e.status & 0xF0, ch = e.status & 0x0F;
            if (type == 0x90 && e.data2 > 0)
                open[(size_t) ch][e.data1 & 0x7F].push_back (i);
            else if (type == 0x80 || type == 0x90)
            {
                auto& stack = open[(size_t) ch][e.data1 & 0x7F];
                if (stack.empty())
                    continue;
                const auto& on = in.events[stack.back()];
                stack.pop_back();
                out->events.push_back ({ mirror (e.frame), (uint8_t) (0x90 | ch), e.data1, on.data2 });
                out->events.push_back ({ mirror (on.frame), (uint8_t) (0x80 | ch), e.data1, 0 });
            }
            else
                out->events.push_back ({ e.frame == 0 ? 0 : mirror (e.frame), e.status, e.data1, e.data2 });
        }
        // Notes still held at the end of the file start at the start.
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                for (const auto i : open[(size_t) ch][(size_t) n])
                {
                    out->events.push_back ({ 0, (uint8_t) (0x90 | ch), (uint8_t) n, in.events[i].data2 });
                    out->events.push_back ({ mirror (in.events[i].frame), (uint8_t) (0x80 | ch), (uint8_t) n, 0 });
                }
        // In time; at the same frame the set-up first, then note-offs, then note-ons.
        auto rank = [] (const MidiClip::Placed& e)
        {
            const auto type = e.status & 0xF0;
            return type == 0x90 ? 2 : type == 0x80 ? 1 : 0;
        };
        std::stable_sort (out->events.begin(), out->events.end(), [&] (const MidiClip::Placed& a, const MidiClip::Placed& b)
        {
            return a.frame != b.frame ? a.frame < b.frame : rank (a) < rank (b);
        });
        return out;
    }

    Clip makeClip (AudioClipPtr audio, Frame start)
    {
        Clip c;
        c.audio = std::move (audio);
        c.start = std::max<Frame> (0, start);
        c.length = c.sourceLength();
        return c;
    }

    Clip makeClip (MidiClipPtr midi, Frame start)
    {
        Clip c;
        c.midi = std::move (midi);
        c.start = std::max<Frame> (0, start);
        c.length = c.sourceLength();
        return c;
    }

    //==========================================================================
    Engine::Engine()
    {
        for (auto* v : { &mixL, &mixR, &trackL, &trackR, &instL, &instR, &monoBuf, &outL, &outR })
            v->resize (block);
        events.reserve (1024);
        liveQueue.reserve (256);
        liveTaken.reserve (256);
        launchQueue.reserve (64);
        launchTaken.reserve (64);
    }

    Engine::~Engine() = default;

    void Engine::prepare (double sampleRate)
    {
        const std::lock_guard<std::mutex> l (lock);
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& t : lanes)
            if (t.instrument != nullptr)
                t.instrument->prepare (rate.load(), block);
        expected = -1;
    }

    void Engine::setNumTracks (int n)
    {
        n = std::clamp (n, 1, maxTracks);
        const std::lock_guard<std::mutex> l (lock);
        // Tracks taken away go quiet at once.
        for (int i = n; i < tracks.load(); ++i)
        {
            auto& t = lanes[(size_t) i];
            if (t.instrument != nullptr)
                t.instrument->reset();
            t.held = {};
        }
        tracks = n;
    }

    void Engine::setClips (int index, Clips list)
    {
        if (index < 0 || index >= maxTracks)
            return;
        list.erase (std::remove_if (list.begin(), list.end(), [] (const Clip& c) { return (c.audio == nullptr) == (c.midi == nullptr) || c.length <= 0; }),
                    list.end());
        for (auto& c : list)
        {
            c.start = std::max<Frame> (0, c.start);
            c.offset = std::clamp<Frame> (c.offset, 0, c.sourceLength());
            c.length = std::clamp<Frame> (c.length, 0, c.sourceLength() - c.offset);
        }
        std::stable_sort (list.begin(), list.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
        std::vector<size_t> cursors (list.size(), 0);
        const std::lock_guard<std::mutex> l (lock);
        auto& t = lanes[(size_t) index];
        const bool hadMidi = std::any_of (t.clips.begin(), t.clips.end(), [] (const Clip& c) { return c.isMidi(); });
        std::swap (t.clips, list);
        std::swap (t.cursors, cursors);
        t.relocate = true;
        // Notes the old clips left sounding are let go.
        if (hadMidi && t.instrument != nullptr)
            t.instrument->releaseAll();
        t.held = {};
    }   // the old clips are freed here, after the lock

    Clips Engine::clips (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return lanes[(size_t) index].clips;
    }

    void Engine::setInstrument (int index, InstrumentPtr instrument)
    {
        if (index < 0 || index >= maxTracks)
            return;
        if (instrument != nullptr)
            instrument->prepare (rate.load(), block);
        const std::lock_guard<std::mutex> l (lock);
        auto& t = lanes[(size_t) index];
        std::swap (t.instrument, instrument);
        t.held = {};
    }

    InstrumentPtr Engine::instrument (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return lanes[(size_t) index].instrument;
    }

    void Engine::play()
    {
        if (! isLooping() && length() > 0 && pos.load() >= length())
            pos = 0;
        stopPending = false;
        if (! playing.exchange (true))
            clockReset = true;
    }

    void Engine::stop()
    {
        if (playing.exchange (false))
            stopPending = true;
    }

    void Engine::setPosition (Frame p) { pos = std::max<Frame> (0, p); }

    void Engine::setLoop (Frame start, Frame end)
    {
        loopStart = std::max<Frame> (0, start);
        loopEnd = end;
    }

    //==========================================================================
    void Engine::setSlot (int track, int scene, SlotPtr s)
    {
        if (track < 0 || track >= maxTracks || scene < 0 || scene >= maxScenes)
            return;
        const std::lock_guard<std::mutex> l (lock);
        auto& t = lanes[(size_t) track];
        std::swap (t.slots[(size_t) scene], s);
        // A playing or waiting slot plays what it holds now; emptied, it stops.
        if (t.playingScene == scene)
        {
            t.playing = t.slots[(size_t) scene];
            if (t.playing == nullptr)
                t.playingScene = -1;
            t.launchChanged = true;
        }
        if (t.queuedScene == scene)
        {
            t.queued = t.slots[(size_t) scene];
            if (t.queued == nullptr)
                t.queuedScene = -1;
        }
    }   // the old slot is freed here, after the lock

    SlotPtr Engine::slot (int track, int scene) const
    {
        if (track < 0 || track >= maxTracks || scene < 0 || scene >= maxScenes)
            return nullptr;
        const std::lock_guard<std::mutex> l (lock);
        return lanes[(size_t) track].slots[(size_t) scene];
    }

    void Engine::launch (int track, int scene)
    {
        if (track < 0 || track >= maxTracks)
            return;
        const bool now = scene >= 0 && ! playing.load();
        {
            const std::lock_guard<std::mutex> l (liveLock);
            if (launchQueue.size() < 256)
                launchQueue.push_back ({ track, scene, now, false });
        }
        if (now)
            play();
    }

    void Engine::launchScene (int scene)
    {
        if (scene < 0 || scene >= maxScenes)
            return;
        const bool now = ! playing.load();
        {
            const std::lock_guard<std::mutex> l (liveLock);
            for (int i = 0; i < tracks.load(); ++i)
                launchQueue.push_back ({ i, scene, now, false });
        }
        if (now)
            play();
    }

    void Engine::stopAll()
    {
        const std::lock_guard<std::mutex> l (liveLock);
        for (int i = 0; i < maxTracks; ++i)
            launchQueue.push_back ({ i, -1, false, false });
    }

    void Engine::backToTimeline (int track)
    {
        const std::lock_guard<std::mutex> l (liveLock);
        for (int i = 0; i < maxTracks; ++i)
            if (track < 0 || i == track)
                launchQueue.push_back ({ i, -1, false, true });
    }

    LaunchInfo Engine::launchInfo (int track) const
    {
        LaunchInfo info;
        if (track < 0 || track >= maxTracks)
            return info;
        const auto& s = launchShared[(size_t) track];
        info.playing = s.playing.load();
        info.queued = s.queued.load();
        info.passes = s.passes.load();
        info.progress = s.progress.load();
        info.timeline = s.timeline.load();
        return info;
    }

    Frame Engine::quantiseFrom (double beats) const
    {
        const auto now = clock.load();
        const auto bpm = std::clamp (tempo.load(), 20.0, 999.0);
        const auto step = beats * 60.0 / bpm * rate.load();
        if (! (step >= 1.0))
            return now;
        const auto k = std::ceil ((double) now / step - 1.0e-9);
        return std::max (now, (Frame) std::llround (k * step));
    }

    void Engine::takeLaunches (bool play)
    {
        const auto now = clock.load();
        for (const auto& r : launchTaken)
        {
            auto& t = lanes[(size_t) r.track];
            if (r.toTimeline)
            {
                t.timeline = true;
                t.playing = t.queued = nullptr;
                t.playingScene = t.queuedScene = -1;
                t.launchChanged = true;
                continue;
            }
            const auto s = r.scene >= 0 && r.scene < maxScenes ? t.slots[(size_t) r.scene] : nullptr;
            if (s == nullptr)
            {
                // A stop, or a scene with nothing for this track: only if something plays or waits.
                if (t.playing == nullptr && t.queuedScene == -1)
                    continue;
                t.queued = nullptr;
                t.queuedScene = stopQueued;
                t.queuedAt = play ? quantiseFrom (launchQuantise.load()) : now;
                continue;
            }
            t.queued = s;
            t.queuedScene = r.scene;
            t.queuedAt = r.now || ! play ? now : quantiseFrom (s->quantise < 0.0 ? launchQuantise.load() : s->quantise);
        }
        launchTaken.clear();

        // What is due now starts (or stops) on this very frame.
        for (auto& t : lanes)
        {
            if (t.queuedScene == -1 || t.queuedAt > now)
                continue;
            if (t.queuedScene == stopQueued)
            {
                t.playing = nullptr;
                t.playingScene = -1;
            }
            else
            {
                t.playing = t.queued;
                t.playingScene = t.queuedScene;
                t.launchedAt = now;
                t.timeline = false;
            }
            t.queued = nullptr;
            t.queuedScene = -1;
            t.launchChanged = true;
        }
    }

    void Engine::placePasses (Track& t, Frame at)
    {
        for (auto& c : t.passClips)
            c = Clip();
        if (t.playing == nullptr || t.playing->clip.length <= 0)
            return;
        const auto& c = t.playing->clip;
        const auto pass = std::max<Frame> (0, at - t.launchedAt) / c.length;
        for (auto k = pass; k <= pass + 1; ++k)
        {
            if (k > 0 && ! t.playing->loop)
                break;
            auto& p = t.passClips[(size_t) (k % 2)];
            p = c;
            p.start = t.launchedAt + k * c.length;
        }
    }

    void Engine::endPasses (int frames)
    {
        const auto now = clock.load();
        for (int i = 0; i < maxTracks; ++i)
        {
            auto& t = lanes[(size_t) i];
            auto& shared = launchShared[(size_t) i];
            if (t.playing != nullptr && ! t.playing->loop && now >= t.launchedAt + t.playing->clip.length)
            {
                t.playing = nullptr;   // played once: the track stops
                t.playingScene = -1;
                t.launchChanged = true;
            }
            const auto length = t.playing != nullptr ? std::max<Frame> (1, t.playing->clip.length) : 1;
            const auto since = std::max<Frame> (0, now - t.launchedAt);
            shared.playing.store (t.playingScene, std::memory_order_relaxed);
            shared.queued.store (t.queuedScene, std::memory_order_relaxed);
            shared.passes.store (t.playing != nullptr ? (int) (since / length) : 0, std::memory_order_relaxed);
            shared.progress.store (t.playing != nullptr ? (float) (since % length) / (float) length : 0.0f, std::memory_order_relaxed);
            shared.timeline.store (t.timeline, std::memory_order_relaxed);
        }
        (void) frames;
    }

    bool Engine::launcherBusy() const
    {
        for (int i = 0; i < tracks.load(); ++i)
            if (lanes[(size_t) i].playing != nullptr || lanes[(size_t) i].queuedScene != -1)
                return true;
        return false;
    }

    Frame Engine::trackEndLocked (int i) const
    {
        Frame end = 0;
        for (const auto& c : lanes[(size_t) i].clips)
            end = std::max (end, c.end());
        return end;
    }

    Frame Engine::trackEnd (int i) const
    {
        if (i < 0 || i >= maxTracks)
            return 0;
        const std::lock_guard<std::mutex> l (lock);
        return trackEndLocked (i);
    }

    Frame Engine::lengthLocked() const
    {
        Frame end = 0;
        for (int i = 0; i < tracks.load(); ++i)
            end = std::max (end, trackEndLocked (i));
        return end;
    }

    Frame Engine::length() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return lengthLocked();
    }

    void Engine::liveMidi (uint8_t status, uint8_t data1, uint8_t data2)
    {
        const std::lock_guard<std::mutex> l (liveLock);
        if (liveQueue.size() < 1024)
            liveQueue.push_back ({ 0, status, data1, data2 });
    }

    void Engine::panic() { panicPending = true; }

    Meter Engine::takeMeter (int index)
    {
        Meter m;
        if (index < 0 || index > masterIndex)
            return m;
        auto& s = meters[(size_t) index];
        for (int c = 0; c < 2; ++c)
        {
            m.peak[c] = s.peak[c].exchange (0.0f, std::memory_order_relaxed);
            m.rms[c] = s.rms[c].exchange (0.0f, std::memory_order_relaxed);
        }
        return m;
    }

    void Engine::meter (int index, const float* left, const float* right, int frames)
    {
        auto& s = meters[(size_t) index];
        const float* side[2] = { left, right };
        for (int c = 0; c < 2; ++c)
        {
            float peak = 0.0f;
            double sum = 0.0;
            for (int i = 0; i < frames; ++i)
            {
                peak = std::max (peak, std::abs (side[c][i]));
                sum += (double) side[c][i] * side[c][i];
            }
            const auto rms = (float) std::sqrt (sum / std::max (1, frames));
            if (peak > s.peak[c].load (std::memory_order_relaxed))
                s.peak[c].store (peak, std::memory_order_relaxed);
            if (rms > s.rms[c].load (std::memory_order_relaxed))
                s.rms[c].store (rms, std::memory_order_relaxed);
        }
    }

    void Engine::releaseHeld (Track& t, std::vector<NoteEvent>& out, int offset, int clip)
    {
        for (int c = 0; c < 16; ++c)
            for (int n = 0; n < 128; ++n)
                if (auto& h = t.held[(size_t) c][(size_t) n]; h != 0 && (clip < 0 || t.owner[(size_t) c][(size_t) n] == clip))
                {
                    out.push_back ({ offset, (uint8_t) (0x80 | c), (uint8_t) (h - 1), 0 });
                    h = 0;
                }
    }

    void Engine::render (float* left, float* right, int frames)
    {
        std::unique_lock<std::mutex> l (lock, std::try_to_lock);
        if (! l.owns_lock())
            return;   // a mixdown, or a clip being swapped

        {
            std::unique_lock<std::mutex> q (liveLock, std::try_to_lock);
            if (q.owns_lock())
            {
                liveTaken.insert (liveTaken.end(), liveQueue.begin(), liveQueue.end());
                liveQueue.clear();
                launchTaken.insert (launchTaken.end(), launchQueue.begin(), launchQueue.end());
                launchQueue.clear();
            }
        }
        // The transport starts: so does the launch clock, with whatever was launched meanwhile.
        if (clockReset.exchange (false))
        {
            clock = 0;
            for (auto& t : lanes)
            {
                t.launchedAt = 0;
                t.queuedAt = std::min<Frame> (t.queuedAt, 0);
            }
        }

        int done = 0;
        while (done < frames)
        {
            const auto at = pos.load();
            const bool play = playing.load();
            auto n = std::min (block, frames - done);
            const auto ls = loopStart.load(), le = loopEnd.load();
            const bool looping = le > ls;
            if (play && looping && at < le)
                n = (int) std::min<Frame> (n, le - at);
            // Launches start on their frame: the block ends where the next one is due.
            takeLaunches (play);
            if (play)
                for (int i = 0; i < maxTracks; ++i)
                    if (const auto& t = lanes[(size_t) i]; t.queuedScene != -1)
                        n = (int) std::min<Frame> (n, std::max<Frame> (1, t.queuedAt - clock.load()));
            renderBlock (outL.data(), outR.data(), n, at, play, true, done);
            if (play)
                clock += n;
            endPasses (n);
            for (int k = 0; k < n; ++k)
            {
                left[done + k] += outL[(size_t) k];
                right[done + k] += outR[(size_t) k];
            }
            liveTaken.clear();
            done += n;
            if (play)
            {
                auto next = at + n;
                if (looping && next >= le)
                    next = ls;   // the next block sees the jump
                else if (! looping && next >= lengthLocked() && ! launcherBusy())
                {
                    playing = false;
                    stopPending = true;
                }
                // Unless the UI moved it meanwhile.
                auto was = at;
                pos.compare_exchange_strong (was, next);
            }
        }
    }

    int Engine::nextBlock (int wanted) const
    {
        const auto at = pos.load();
        const auto ls = loopStart.load(), le = loopEnd.load();
        if (playing.load() && le > ls && at < le)
            return (int) std::max<Frame> (1, std::min<Frame> (wanted, le - at));
        return wanted;
    }

    void Engine::renderBlock (float* left, float* right, int frames, Frame at, bool play, bool withLive, int blockOffset)
    {
        auto* routed = withLive ? patch.load() : nullptr;
        const auto count = tracks.load();
        const bool jumped = at != expected;
        const bool stopping = stopPending.exchange (false);
        const bool panicking = panicPending.exchange (false);
        expected = play ? at + frames : at;
        if (stopping && withLive)
            for (auto& t : lanes)
            {
                if (t.playing != nullptr || t.queuedScene != -1)
                    t.launchChanged = true;
                t.playing = t.queued = nullptr;
                t.playingScene = t.queuedScene = -1;
            }

        bool anySolo = false;
        for (int i = 0; i < count; ++i)
            anySolo = anySolo || strips[(size_t) i].solo.load();

        std::fill (mixL.begin(), mixL.begin() + frames, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + frames, 0.0f);
        const auto armedTrack = armed.load();

        for (int slot = 0; slot < (routed != nullptr ? maxTracks : count); ++slot)
        {
            const auto i = routed != nullptr ? routed->trackAt (slot) : slot;
            if (i < 0 || i >= count)
                continue;
            if (only >= 0 && i != only)
                continue;   // a bounce: this track alone
            auto& t = lanes[(size_t) i];
            auto& s = strips[(size_t) i];
            events.clear();

            if (panicking)
            {
                t.held = {};
                if (t.instrument != nullptr)
                    t.instrument->reset();
            }

            // A launched track plays its launcher clip's passes, placed on the launch clock;
            // a mixdown always plays the timeline.
            const bool launched = withLive && ! t.timeline;
            if (launched)
                placePasses (t, clock.load());
            const auto& clips = launched ? t.passClips : t.clips;
            auto& cursors = launched ? t.passCursors : t.cursors;
            const auto base = launched ? clock.load() : at;
            const bool trackJumped = (launched ? false : jumped) || t.launchChanged;
            t.launchChanged = false;

            // MIDI from the clips: let go of what they held when the transport stops, jumps or loses channels.
            const bool playMidi = play && std::any_of (clips.begin(), clips.end(), [] (const Clip& c) { return c.isMidi(); });
            const auto channels = s.channels.load();
            if ((t.wasPlayingMidi || launched) && (trackJumped || ! playMidi || stopping || channels != t.lastChannels))
                releaseHeld (t, events);
            if (trackJumped || ! playMidi || launched)
                t.relocate = true;   // the passes are found again each block
            t.wasPlayingMidi = playMidi;
            t.lastChannels = channels;
            if (stopping && t.instrument != nullptr)
                t.instrument->releaseAll();

            if (playMidi)
            {
                const auto transpose = s.transpose.load();
                for (size_t ci = 0; ci < clips.size(); ++ci)
                {
                    const auto& c = clips[ci];
                    if (! c.isMidi())
                        continue;
                    // The clip's own frames: where the block starts in the file.
                    const auto local = base - c.start + c.offset;
                    const auto& placed = c.midi->events;
                    auto& cursor = cursors[ci];
                    if (t.relocate)
                        cursor = (size_t) (std::lower_bound (placed.begin(), placed.end(), std::max (local, c.offset),
                                                             [] (const MidiClip::Placed& e, Frame f) { return e.frame < f; }) - placed.begin());
                    if (c.start >= base + frames || c.end() <= base)
                        continue;
                    const auto stopAt = std::min (local + frames, c.offset + c.length);
                    while (! c.muted && cursor < placed.size() && placed[cursor].frame < stopAt)
                    {
                        const auto& e = placed[cursor++];
                        if (e.frame < c.offset)
                            continue;
                        const auto ch = e.status & 0x0F;
                        const auto offset = (int) std::max<Frame> (0, e.frame - local);
                        const auto kind = e.status & 0xF0;
                        if (kind == 0x80)
                        {
                            auto& h = t.held[(size_t) ch][e.data1];
                            if (h != 0 && t.owner[(size_t) ch][e.data1] == ci)
                            {
                                events.push_back ({ offset, e.status, (uint8_t) (h - 1), e.data2 });
                                h = 0;
                            }
                            continue;
                        }
                        if ((channels & (1u << ch)) == 0)
                            continue;
                        if (kind == 0x90)
                        {
                            const auto note = std::clamp (e.data1 + (ch == 9 ? 0 : transpose), 0, 127);
                            auto& h = t.held[(size_t) ch][e.data1];
                            if (h != 0)
                                events.push_back ({ offset, (uint8_t) (0x80 | ch), (uint8_t) (h - 1), 0 });   // the same note again
                            h = (uint8_t) (note + 1);
                            t.owner[(size_t) ch][e.data1] = (uint16_t) ci;
                            events.push_back ({ offset, e.status, (uint8_t) note, e.data2 });
                            continue;
                        }
                        events.push_back ({ offset, e.status, e.data1, e.data2 });
                    }
                    // A clip that ends (or is trimmed) in this block lets go of its notes there.
                    if (c.end() > base && c.end() <= base + frames)
                        releaseHeld (t, events, (int) std::min<Frame> (frames - 1, c.end() - base), (int) ci);
                }
                t.relocate = false;
            }

            // The keyboard plays the armed track at the start of the block.
            if (withLive && i == armedTrack && ! liveTaken.empty())
                events.insert (events.begin(), liveTaken.begin(), liveTaken.end());
            std::stable_sort (events.begin(), events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.offset < b.offset; });

            std::fill (instL.begin(), instL.begin() + frames, 0.0f);
            std::fill (instR.begin(), instR.begin() + frames, 0.0f);
            if (t.instrument != nullptr)
                t.instrument->process (events.data(), (int) events.size(), instL.data(), instR.data(), frames);
            if (routed != nullptr)
                routed->trackInput (i, blockOffset, instL.data(), instR.data(), frames);   // through the strip like the instrument

            // The audio clips: stereo ones join the instrument (both are balanced), mono ones
            // go apart to be panned with equal power. A short fade at each edge keeps a
            // trimmed clip from clicking.
            std::fill (monoBuf.begin(), monoBuf.begin() + frames, 0.0f);
            bool hasMono = false;
            if (play)
                for (const auto& c : clips)
                {
                    if (c.audio == nullptr || c.muted || c.start >= base + frames || c.end() <= base)
                        continue;
                    const auto from = std::max (base, c.start), to = std::min (base + frames, c.end());
                    const auto fade = std::max<Frame> (1, std::min<Frame> (96, c.length / 4));
                    const auto& a = *c.audio;
                    const auto last = a.length() - 1;
                    for (auto f = from; f < to; ++f)
                    {
                        const auto forward = f - c.start + c.offset;
                        const auto src = (size_t) (c.reversed ? last - forward : forward);
                        const auto edge = std::min (f - c.start, c.end() - 1 - f);
                        const auto g = edge < fade ? (float) (edge + 1) / (float) (fade + 1) : 1.0f;
                        const auto k = (size_t) (f - base);
                        if (a.mono)
                            monoBuf[k] += a.left[src] * g;
                        else
                        {
                            instL[k] += a.left[src] * g;
                            instR[k] += a.right[src] * g;
                        }
                    }
                    hasMono = hasMono || a.mono;
                }

            // Trim, polarity, pan, fader, mute and solo, as one gain a side for each source.
            // A bounce takes the track before its fader and pan (trim and polarity included),
            // whatever mute and solo say; a mono file is then on both sides at full level.
            const bool bouncing = only >= 0;
            const bool audible = bouncing || (! s.mute.load() && (! anySolo || s.solo.load()));
            const auto level = audible ? dbToGain (s.trimDb.load()) * (bouncing ? 1.0f : dbToGain (s.faderDb.load())) : 0.0f;
            const auto pan = bouncing ? 0.0f : std::clamp (s.pan.load(), -1.0f, 1.0f);
            const auto signL = s.invertLeft.load() ? -1.0f : 1.0f, signR = s.invertRight.load() ? -1.0f : 1.0f;
            const auto balanceL = std::min (1.0f, 1.0f - pan), balanceR = std::min (1.0f, 1.0f + pan);
            const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
            const auto monoL = level * signL * (bouncing ? 1.0f : std::cos (angle));
            const auto monoR = level * signR * (bouncing ? 1.0f : std::sin (angle));
            const auto gainL = level * signL * balanceL, gainR = level * signR * balanceR;

            // After a jump there is nothing to ramp from: the new place starts at its level.
            if (jumped)
            {
                t.monoL = monoL;
                t.monoR = monoR;
                t.gainL = gainL;
                t.gainR = gainR;
            }

            // The track's own sum, for its meter, then into the mix.
            auto* sumL = trackL.data();
            auto* sumR = trackR.data();
            std::fill (sumL, sumL + frames, 0.0f);
            std::fill (sumR, sumR + frames, 0.0f);
            addRamped (sumL, instL.data(), frames, t.gainL, gainL);
            addRamped (sumR, instR.data(), frames, t.gainR, gainR);
            if (hasMono)
            {
                addRamped (sumL, monoBuf.data(), frames, t.monoL, monoL);
                addRamped (sumR, monoBuf.data(), frames, t.monoR, monoR);
            }
            t.monoL = monoL;
            t.monoR = monoR;
            t.gainL = gainL;
            t.gainR = gainR;

            if (withLive)
                meter (i, sumL, sumR, frames);
            if (routed != nullptr)
            {
                routed->trackOutput (i, blockOffset, sumL, sumR, frames);
                continue;
            }
            for (int k = 0; k < frames; ++k)
            {
                mixL[(size_t) k] += sumL[k];
                mixR[(size_t) k] += sumR[k];
            }
        }
        if (routed != nullptr)
        {
            std::fill (left, left + frames, 0.0f);   // the patch has the tracks, and does the master
            std::fill (right, right + frames, 0.0f);
            return;
        }

        const auto master = only >= 0 ? 1.0f : dbToGain (masterDb.load());
        if (jumped)
            masterGain = master;
        for (int k = 0; k < frames; ++k)
        {
            const auto g = masterGain + (master - masterGain) * (float) (k + 1) / (float) frames;
            left[k] = mixL[(size_t) k] * g;
            right[k] = mixR[(size_t) k] * g;
        }
        masterGain = master;
        if (withLive)
            meter (masterIndex, left, right, frames);
    }

    AudioDataPtr Engine::mixdown (double tailSeconds, std::string& error, std::atomic<float>* progress, const std::atomic<bool>* cancel,
                                  int onlyTrack)
    {
        const std::lock_guard<std::mutex> l (lock);
        const auto r = rate.load();
        only = onlyTrack >= 0 && onlyTrack < tracks.load() ? onlyTrack : -1;
        const auto end = only >= 0 ? trackEndLocked (only) : lengthLocked();
        if (end <= 0)
        {
            error = only >= 0 ? "There is nothing on the track to bounce." : "There is nothing on the tracks to mix down.";
            only = -1;
            return nullptr;
        }
        for (auto& t : lanes)
        {
            if (t.instrument != nullptr)
                t.instrument->reset();
            t.held = {};
            t.relocate = true;
        }
        stopPending = false;
        expected = -1;

        // After the end, it goes on while the instruments still ring (a quarter second of silence ends it).
        const auto maxTail = (Frame) std::llround (std::max (0.0, tailSeconds) * r);
        const auto quietNeeded = (Frame) std::llround (0.25 * r);
        AudioData::Builder out (2, r);
        std::vector<float> left (block), right (block);
        Frame at = 0, quiet = 0;
        while (at < end + maxTail)
        {
            if (cancel != nullptr && cancel->load())
            {
                error = "Cancelled.";
                break;
            }
            const auto n = (int) std::min<Frame> (block, end + maxTail - at);
            renderBlock (left.data(), right.data(), n, at, true, false);
            const float* ptrs[2] = { left.data(), right.data() };
            out.append (ptrs, n);
            at += n;
            if (at > end)
            {
                float peak = 0.0f;
                for (int k = 0; k < n; ++k)
                    peak = std::max ({ peak, std::abs (left[(size_t) k]), std::abs (right[(size_t) k]) });
                quiet = peak < 1.0e-5f ? quiet + n : 0;
                if (quiet >= quietNeeded)
                    break;
            }
            if (progress != nullptr)
                progress->store ((float) std::min (1.0, (double) at / (double) end));
        }

        for (auto& t : lanes)
        {
            if (t.instrument != nullptr)
                t.instrument->reset();
            t.held = {};
            t.relocate = true;
        }
        expected = -1;
        only = -1;
        for (auto& m : meters)
            for (int c = 0; c < 2; ++c)
            {
                m.peak[c] = 0.0f;
                m.rms[c] = 0.0f;
            }
        if (! error.empty())
            return nullptr;
        return out.finish();
    }
}
