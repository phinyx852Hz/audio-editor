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

    //==========================================================================
    Engine::Engine()
    {
        for (auto* v : { &mixL, &mixR, &trackL, &trackR, &instL, &instR, &outL, &outR })
            v->resize (block);
        events.reserve (1024);
        liveQueue.reserve (256);
        liveTaken.reserve (256);
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

    void Engine::setAudio (int index, AudioClipPtr clip)
    {
        if (index < 0 || index >= maxTracks)
            return;
        const std::lock_guard<std::mutex> l (lock);
        std::swap (lanes[(size_t) index].audio, clip);
    }   // the old clip is freed here, after the lock

    void Engine::setMidi (int index, MidiClipPtr clip)
    {
        if (index < 0 || index >= maxTracks)
            return;
        const std::lock_guard<std::mutex> l (lock);
        auto& t = lanes[(size_t) index];
        std::swap (t.midi, clip);
        t.relocate = true;
        // Notes the old clip left sounding are let go.
        if (t.instrument != nullptr)
            t.instrument->releaseAll();
        t.held = {};
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

    AudioClipPtr Engine::audio (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return lanes[(size_t) index].audio;
    }

    MidiClipPtr Engine::midi (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return lanes[(size_t) index].midi;
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
        playing = true;
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

    Frame Engine::trackEndLocked (int i) const
    {
        Frame end = 0;
        const auto r = rate.load();
        const auto& t = lanes[(size_t) i];
        const auto& s = strips[(size_t) i];
        if (t.audio != nullptr)
            end = std::max (end, (Frame) std::llround (s.audioOffset.load() * r) + t.audio->length());
        if (t.midi != nullptr)
            end = std::max (end, (Frame) std::llround (s.midiOffset.load() * r) + t.midi->length);
        return end;
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

    void Engine::releaseHeld (Track& t, std::vector<NoteEvent>& out)
    {
        for (int c = 0; c < 16; ++c)
            for (int n = 0; n < 128; ++n)
                if (auto& h = t.held[(size_t) c][(size_t) n]; h != 0)
                {
                    out.push_back ({ 0, (uint8_t) (0x80 | c), (uint8_t) (h - 1), 0 });
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
            renderBlock (outL.data(), outR.data(), n, at, play, true);
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
                else if (! looping && next >= lengthLocked())
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

    void Engine::renderBlock (float* left, float* right, int frames, Frame at, bool play, bool withLive)
    {
        const auto r = rate.load();
        const auto count = tracks.load();
        const bool jumped = at != expected;
        const bool stopping = stopPending.exchange (false);
        const bool panicking = panicPending.exchange (false);
        expected = play ? at + frames : at;

        bool anySolo = false;
        for (int i = 0; i < count; ++i)
            anySolo = anySolo || strips[(size_t) i].solo.load();

        std::fill (mixL.begin(), mixL.begin() + frames, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + frames, 0.0f);
        const auto armedTrack = armed.load();

        for (int i = 0; i < count; ++i)
        {
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

            // MIDI from the clip: let go of what it held when it stops, jumps or loses channels.
            const bool playMidi = play && s.midiOn.load() && t.midi != nullptr;
            const auto channels = s.channels.load();
            if (t.wasPlayingMidi && (jumped || ! playMidi || stopping || channels != t.lastChannels))
                releaseHeld (t, events);
            if (jumped || ! playMidi)
                t.relocate = true;
            t.wasPlayingMidi = playMidi;
            t.lastChannels = channels;
            if (stopping && t.instrument != nullptr)
                t.instrument->releaseAll();

            if (playMidi)
            {
                const auto local = at - (Frame) std::llround (s.midiOffset.load() * r);
                const auto& placed = t.midi->events;
                if (t.relocate)
                {
                    t.cursor = (size_t) (std::lower_bound (placed.begin(), placed.end(), local,
                                                           [] (const MidiClip::Placed& e, Frame f) { return e.frame < f; }) - placed.begin());
                    t.relocate = false;
                }
                const auto transpose = s.transpose.load();
                while (t.cursor < placed.size() && placed[t.cursor].frame < local + frames)
                {
                    const auto& e = placed[t.cursor++];
                    const auto ch = e.status & 0x0F;
                    const auto offset = (int) std::max<Frame> (0, e.frame - local);
                    const auto kind = e.status & 0xF0;
                    if (kind == 0x80)
                    {
                        auto& h = t.held[(size_t) ch][e.data1];
                        if (h != 0)
                            events.push_back ({ offset, e.status, (uint8_t) (h - 1), e.data2 });
                        h = 0;
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
                        events.push_back ({ offset, e.status, (uint8_t) note, e.data2 });
                        continue;
                    }
                    events.push_back ({ offset, e.status, e.data1, e.data2 });
                }
            }

            // The keyboard plays the armed track at the start of the block.
            if (withLive && i == armedTrack && ! liveTaken.empty())
                events.insert (events.begin(), liveTaken.begin(), liveTaken.end());
            std::stable_sort (events.begin(), events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.offset < b.offset; });

            // The audio clip.
            std::fill (trackL.begin(), trackL.begin() + frames, 0.0f);
            std::fill (trackR.begin(), trackR.begin() + frames, 0.0f);
            bool mono = false, hasAudio = false;
            if (play && s.audioOn.load() && t.audio != nullptr)
            {
                const auto local = at - (Frame) std::llround (s.audioOffset.load() * r);
                const auto len = t.audio->length();
                const auto from = std::max<Frame> (local, 0), to = std::min<Frame> (local + frames, len);
                if (to > from)
                {
                    std::copy (t.audio->left.begin() + from, t.audio->left.begin() + to, trackL.begin() + (from - local));
                    std::copy (t.audio->right.begin() + from, t.audio->right.begin() + to, trackR.begin() + (from - local));
                    hasAudio = true;
                }
                mono = t.audio->mono;
            }

            std::fill (instL.begin(), instL.begin() + frames, 0.0f);
            std::fill (instR.begin(), instR.begin() + frames, 0.0f);
            if (t.instrument != nullptr)
                t.instrument->process (events.data(), (int) events.size(), instL.data(), instR.data(), frames);

            // Trim, polarity, pan, fader, mute and solo, as one gain a side for each source.
            // A bounce takes the track before its fader and pan (trim and polarity included),
            // whatever mute and solo say; a mono file is then on both sides at full level.
            const bool bouncing = only >= 0;
            const bool audible = bouncing || (! s.mute.load() && (! anySolo || s.solo.load()));
            const auto level = audible ? dbToGain (s.trimDb.load()) * (bouncing ? 1.0f : dbToGain (s.faderDb.load())) : 0.0f;
            const auto pan = bouncing ? 0.0f : std::clamp (s.pan.load(), -1.0f, 1.0f);
            if (bouncing)
                mono = false;
            const auto signL = s.invertLeft.load() ? -1.0f : 1.0f, signR = s.invertRight.load() ? -1.0f : 1.0f;
            const auto balanceL = std::min (1.0f, 1.0f - pan), balanceR = std::min (1.0f, 1.0f + pan);
            const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
            const auto audioL = level * signL * (mono ? std::cos (angle) : balanceL);
            const auto audioR = level * signR * (mono ? std::sin (angle) : balanceR);
            const auto instGainL = level * signL * balanceL, instGainR = level * signR * balanceR;

            // After a jump there is nothing to ramp from: the new place starts at its level.
            if (jumped)
            {
                t.audioL = audioL;
                t.audioR = audioR;
                t.gainL = instGainL;
                t.gainR = instGainR;
            }

            // The track's own sum, for its meter, then into the mix.
            auto* sumL = trackL.data();
            auto* sumR = trackR.data();
            if (hasAudio)
            {
                for (int k = 0; k < frames; ++k)
                {
                    const auto step = (float) (k + 1) / (float) frames;
                    sumL[k] *= t.audioL + (audioL - t.audioL) * step;
                    sumR[k] *= t.audioR + (audioR - t.audioR) * step;
                }
            }
            addRamped (sumL, instL.data(), frames, t.gainL, instGainL);
            addRamped (sumR, instR.data(), frames, t.gainR, instGainR);
            t.audioL = audioL;
            t.audioR = audioR;
            t.gainL = instGainL;
            t.gainR = instGainR;

            if (withLive)
                meter (i, sumL, sumR, frames);
            for (int k = 0; k < frames; ++k)
            {
                mixL[(size_t) k] += sumL[k];
                mixR[(size_t) k] += sumR[k];
            }
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
