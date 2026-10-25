// SPDX-License-Identifier: AGPL-3.0-only
// The mixer's recording (docs/recording.md): takes into launcher slots and onto the timeline,
// the audio input and its monitoring, Capture, the metronome and the count-in. Everything a
// take does in time happens on the audio thread, to the frame; the room it needs is made
// beforehand on the UI thread, so the audio thread never allocates.
#include "MixerEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mixer
{
    namespace
    {
        constexpr size_t fifoFrames = 16384;       // the audio input not yet used
        constexpr int keepInput = 2048;            // more than this beyond a block is dropped: the delay stays bounded
        constexpr double streamSeconds = 10.0;     // a timeline take's ring, emptied by the UI every tick
        constexpr double earlySeconds = 0.15;      // a note this early for a take's first beat goes on it
        constexpr size_t markRoom = 16384;
        constexpr size_t reportRoom = 64;
        constexpr size_t takeNotes = 16384;        // a slot take's room for MIDI messages

        bool isNoteOn (uint8_t status, uint8_t velocity) { return (status & 0xF0) == 0x90 && velocity > 0; }
        bool isNoteOff (uint8_t status, uint8_t velocity) { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && velocity == 0); }

        // In time; at the same frame the set-up first, then note-offs, then note-ons.
        int rank (const MidiClip::Placed& e)
        {
            const auto type = e.status & 0xF0;
            return type == 0x90 ? 2 : type == 0x80 ? 1 : 0;
        }
        bool before (const MidiClip::Placed& a, const MidiClip::Placed& b)
        {
            return a.frame != b.frame ? a.frame < b.frame : rank (a) < rank (b);
        }

        // Into the clip's events in order, within the room reserved (never allocating).
        void insertPlaced (MidiClip& clip, MidiClip::Placed e)
        {
            auto& v = clip.events;
            if (v.size() >= v.capacity())
                return;
            v.push_back (e);
            for (auto k = v.size() - 1; k > 0 && before (v[k], v[k - 1]); --k)
                std::swap (v[k], v[k - 1]);
        }

        Frame roundTo (Frame f, double step)
        {
            return step >= 1.0 ? (Frame) std::llround (std::round ((double) f / step) * step) : f;
        }
    }

    midifile::SongPtr songOf (const MidiClip& clip, double sampleRate, double bpm)
    {
        constexpr int tpq = 480;
        bpm = std::clamp (bpm, 20.0, 999.0);
        const auto framesPerTick = 60.0 / bpm * sampleRate / tpq;
        auto song = std::make_shared<midifile::Song>();
        song->ticksPerQuarter = tpq;
        song->tempos.push_back ({ 0, 60.0e6 / bpm });
        song->events.reserve (clip.events.size());
        for (const auto& e : clip.events)
        {
            song->events.push_back ({ (int64_t) std::llround ((double) e.frame / framesPerTick), e.status, e.data1, e.data2 });
            if (isNoteOn (e.status, e.data2))
                song->channelsUsed = (uint16_t) (song->channelsUsed | (1u << (e.status & 0x0F)));
        }
        song->lastTick = std::max<int64_t> ((int64_t) std::llround ((double) clip.length / framesPerTick),
                                            song->events.empty() ? 0 : song->events.back().tick);
        return song;
    }

    //==========================================================================
    bool Engine::recordsAudio (int index) const
    {
        const auto& s = strips[(size_t) index];
        const auto in = s.input.load();
        if (in == Strip::inputAudio || in == Strip::inputBoth)
            return true;
        if (in == Strip::inputMidi)
            return false;
        return lanes[(size_t) index].instrument == nullptr || (TrackMode) s.mode.load() == TrackMode::audio;
    }

    void Engine::setInput (Input* in)
    {
        input = in;
        const std::lock_guard<std::mutex> l (lock);   // render holds it while it reads the input
        inputHave = 0;
    }

    void Engine::takeInput (int frames)
    {
        std::fill (inL.begin(), inL.begin() + frames, 0.0f);
        std::fill (inR.begin(), inR.begin() + frames, 0.0f);
        const auto n = (int) std::min<size_t> ((size_t) frames, inputHave);
        for (int k = 0; k < n; ++k)
        {
            inL[(size_t) k] = inputFifo[(size_t) (2 * k)];
            inR[(size_t) k] = inputFifo[(size_t) (2 * k + 1)];
        }
        if (n > 0)
        {
            std::memmove (inputFifo.data(), inputFifo.data() + 2 * n, (inputHave - (size_t) n) * 2 * sizeof (float));
            inputHave -= (size_t) n;
        }
    }

    bool Engine::record (const RecordRequest& r, std::string& error)
    {
        if (r.track < 0 || r.track >= tracks.load())
        {
            error = "There is no such track.";
            return false;
        }
        if (! r.midi && ! r.audio)
        {
            error = "The track records nothing: choose MIDI or audio for its input.";
            return false;
        }
        const bool timeline = r.scene < 0;
        if (r.scene >= maxScenes)
        {
            error = "There is no such scene.";
            return false;
        }
        if (r.overdub && (timeline || (r.overdubAudio == nullptr && r.overdubMidi == nullptr)))
        {
            error = "Overdub needs the slot's clip playing.";
            return false;
        }
        const auto sr = rate.load();
        const auto beatFrames = 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * sr;

        // The room, made here: the audio thread only writes into it.
        Track::Take k;
        k.request = r;
        k.timeline = timeline;
        k.length = r.beats > 0.0 ? std::max<Frame> (1, (Frame) std::llround (r.beats * beatFrames)) : 0;
        k.room = k.length > 0 ? k.length : (Frame) std::llround (std::max (1.0, r.maxSeconds) * sr);
        for (auto& c : k.on)
            c.fill (-1);
        for (auto& c : k.shift)
            c.fill (0);
        auto makeRoom = [&] (std::shared_ptr<AudioClip>& audio, std::shared_ptr<MidiClip>& midi, std::shared_ptr<Slot>& slot)
        {
            if (r.audio && ! r.midi)   // a slot holds one clip: the keyboard's when both are asked for
            {
                audio = std::make_shared<AudioClip>();
                audio->left.assign ((size_t) k.room, 0.0f);
                audio->right.assign ((size_t) k.room, 0.0f);
                audio->mono = strips[(size_t) r.track].inputChannels.load() != 0;
            }
            else
            {
                midi = std::make_shared<MidiClip>();
                midi->events.reserve (takeNotes);
                midi->bpm = tempo.load();
                midi->length = k.length;
            }
            slot = std::make_shared<Slot>();
        };
        if (r.overdub)
        {
            k.audio = r.overdubAudio;
            k.midi = r.overdubMidi;
        }
        else if (! timeline)
            makeRoom (k.audio, k.midi, k.slot);
        else if (streamRing.size() < (size_t) (2.0 * streamSeconds * sr))
        {
            const std::lock_guard<std::mutex> l (lock);
            streamRing.assign ((size_t) (2.0 * streamSeconds * sr), 0.0f);
        }

        const std::lock_guard<std::mutex> l (lock);
        auto& t = lanes[(size_t) r.track];
        auto& now = t.take;
        // While a slot take records, the next one asked for follows it on the frame it ends.
        if (! timeline && ! r.overdub && now.state != RecordState::none && ! now.timeline && now.request.scene != r.scene
            && now.state != RecordState::overdubbing)
        {
            makeRoom (now.nextAudio, now.nextMidi, now.nextSlot);   // allocates under the lock, once, on the UI thread
            now.nextScene = r.scene;
            return true;
        }
        if (now.state != RecordState::none || now.reporting || now.tailTo >= 0)
        {
            error = "The track is recording already.";
            return false;
        }
        if (timeline)
            for (const auto& other : lanes)
                if (other.take.state != RecordState::none && other.take.timeline)
                {
                    error = "Another track is recording onto the timeline.";
                    return false;
                }
        if (r.overdub)
        {
            if (t.playingScene != r.scene || t.playing == nullptr
                || (t.playing->clip.audio != r.overdubAudio && t.playing->clip.midi != r.overdubMidi))
            {
                error = "Overdub needs the slot's clip playing.";
                return false;
            }
            k.state = RecordState::overdubbing;
            k.room = t.playing->clip.length;
        }
        else
        {
            k.state = RecordState::waiting;
            // On the quantise while the transport plays; else as it starts (after any count-in).
            k.startAt = timeline || ! playing.load() ? -1 : quantiseFrom (launchQuantise.load());
        }
        if (timeline)
        {
            streamRead = streamWritten.load();
            gathered = {};
            gathered.track = r.track;
            gathered.audioOn = r.audio;
            gathered.midiOn = r.midi;
            gathered.quantise = r.quantise;
        }
        // Whatever the track kept from before (a tail long done, a report given) goes after the lock.
        std::swap (now, k);
        auto& shared = recordShared[(size_t) r.track];
        shared.state = (int) now.state;
        shared.scene = r.scene;
        shared.progress = 0.0f;
        shared.recorded = 0;
        shared.length = now.length;
        return true;
    }

    void Engine::stopRecording (int track)
    {
        const std::lock_guard<std::mutex> l (lock);
        for (int i = 0; i < maxTracks; ++i)
        {
            if (track >= 0 && i != track)
                continue;
            auto& k = lanes[(size_t) i].take;
            if (k.state == RecordState::waiting)
            {
                // Not begun: nothing to keep.
                k.state = RecordState::none;
                k.nextScene = -1;
                recordShared[(size_t) i].state = 0;
                if (k.timeline)
                    gathered = {};
            }
            else if (k.state != RecordState::none)
            {
                k.stopAsked = true;
                k.nextScene = -1;   // the room made for it goes with the next take asked for
            }
        }
    }

    RecordInfo Engine::recordInfo (int track) const
    {
        RecordInfo info;
        if (track < 0 || track >= maxTracks)
            return info;
        const auto& s = recordShared[(size_t) track];
        info.state = (RecordState) s.state.load();
        info.scene = s.scene.load();
        info.progress = s.progress.load();
        info.recorded = s.recorded.load();
        info.length = s.length.load();
        info.start = s.start.load();
        return info;
    }

    bool Engine::isRecording() const
    {
        for (const auto& s : recordShared)
            if (s.state.load() != 0)
                return true;
        return false;
    }

    void Engine::playWithCountIn (double bars)
    {
        const auto beatFrames = 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * rate.load();
        const auto frames = (Frame) std::llround (std::max (0.0, bars) * std::max (1, beatsPerBar.load()) * beatFrames);
        if (frames <= 0 || playing.load())
        {
            play();
            return;
        }
        const std::lock_guard<std::mutex> l (lock);
        countTotal = frames;
        countLeft = frames;
    }

    std::vector<Recorded> Engine::takeRecorded()
    {
        std::vector<Recorded> out;
        std::vector<SlotPtr> freed;   // let go of here, after the lock
        std::vector<Mark> came;
        {
            const std::lock_guard<std::mutex> l (liveLock);
            std::swap (out, recordedOut);
            std::swap (freed, slotsOut);
            std::swap (came, marks);
            recordedOut.reserve (reportRoom);   // the audio thread only pushes within this room
            slotsOut.reserve (reportRoom);
            marks.reserve (markRoom);
        }
        // The timeline take's audio, from the ring.
        const auto written = streamWritten.load (std::memory_order_acquire);
        auto read = streamRead.load();
        const auto ringFrames = streamRing.size() / 2;
        if (gathered.track >= 0 && ringFrames > 0)
            for (; read < written; ++read)
            {
                const auto at = (size_t) (read % ringFrames) * 2;
                gathered.audio.push_back (streamRing[at]);
                gathered.audio.push_back (streamRing[at + 1]);
            }
        streamRead.store (written, std::memory_order_release);

        const auto sr = rate.load();
        const auto latency = (int64_t) std::llround (std::max (0.0f, inputLatencyMs.load()) * 0.001 * sr);
        for (const auto& m : came)
        {
            if (m.track != gathered.track)
                continue;
            gathered.marks.push_back (m);
            if (m.kind != Mark::end)
                continue;
            // The take has ended: a clip (or two, the audio and the notes) for each pass.
            const auto beatFrames = 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * sr;
            const auto grid = gathered.quantise * beatFrames;
            std::vector<const Mark*> passes;
            for (const auto& g : gathered.marks)
                if (g.kind != Mark::note)
                    passes.push_back (&g);
            for (size_t p = 0; p + 1 < passes.size(); ++p)
            {
                const auto& from = *passes[p];
                const auto& to = *passes[p + 1];
                const auto frames = (Frame) (to.audioAt - from.audioAt);
                if (frames <= (Frame) (0.01 * sr))
                    continue;   // a pass of nothing: a jump straight after another
                Recorded rec;
                rec.track = gathered.track;
                rec.start = from.frame;
                rec.length = frames;
                rec.pass = (int) p;
                if (gathered.audioOn)
                {
                    auto clip = std::make_shared<AudioClip>();
                    clip->left.assign ((size_t) frames, 0.0f);
                    clip->right.assign ((size_t) frames, 0.0f);
                    const auto channels = strips[(size_t) gathered.track].inputChannels.load();
                    clip->mono = channels != 0;
                    const auto have = (int64_t) gathered.audio.size() / 2;
                    for (Frame f = 0; f < frames; ++f)
                    {
                        const auto src = from.audioAt + latency + f;
                        if (src < 0 || src >= have)
                            continue;
                        const auto l = gathered.audio[(size_t) (2 * src)], r = gathered.audio[(size_t) (2 * src + 1)];
                        clip->left[(size_t) f] = channels == 2 ? r : l;
                        clip->right[(size_t) f] = channels == 1 ? l : r;
                    }
                    rec.audio = clip;
                }
                if (gathered.midiOn)
                {
                    auto clip = std::make_shared<MidiClip>();
                    clip->bpm = tempo.load();
                    clip->length = frames;
                    std::array<std::array<Frame, 128>, 16> on, shift {};
                    for (auto& c : on)
                        c.fill (-1);
                    for (const auto& g : gathered.marks)
                    {
                        if (g.kind != Mark::note || g.audioAt < from.audioAt || g.audioAt >= to.audioAt)
                            continue;
                        const auto played = g.frame - from.frame;
                        const auto ch = (size_t) (g.status & 0x0F), key = (size_t) (g.data1 & 0x7F);
                        if (isNoteOn (g.status, g.data2))
                        {
                            // On the grid of the timeline's own beats.
                            const auto at = std::clamp<Frame> (roundTo (g.frame, grid) - from.frame, 0, frames - 1);
                            if (on[ch][key] >= 0)
                                clip->events.push_back ({ std::max (at, on[ch][key] + 1), (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
                            on[ch][key] = at;
                            shift[ch][key] = at - played;
                            clip->events.push_back ({ at, g.status, g.data1, g.data2 });
                            rec.notes = true;
                        }
                        else if (isNoteOff (g.status, g.data2))
                        {
                            if (on[ch][key] < 0)
                                continue;   // begun before the pass
                            const auto at = std::clamp<Frame> (played + shift[ch][key], on[ch][key] + 1, frames - 1);
                            clip->events.push_back ({ at, (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
                            on[ch][key] = -1;
                        }
                        else
                            clip->events.push_back ({ std::clamp<Frame> (played, 0, frames - 1), g.status, g.data1, g.data2 });
                    }
                    for (size_t ch = 0; ch < 16; ++ch)
                        for (size_t key = 0; key < 128; ++key)
                            if (on[ch][key] >= 0)
                                clip->events.push_back ({ std::max (frames - 1, on[ch][key] + 1), (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
                    std::stable_sort (clip->events.begin(), clip->events.end(), before);
                    if (! clip->events.empty())
                        rec.midi = clip;
                }
                if (rec.audio != nullptr || rec.midi != nullptr)
                    out.push_back (rec);
            }
            gathered = {};
        }
        return out;
    }

    MidiClipPtr Engine::capture (int index, Frame& phase)
    {
        phase = 0;
        if (index < 0 || index >= maxTracks)
            return nullptr;
        std::vector<Kept> played;
        {
            const std::lock_guard<std::mutex> l (lock);
            for (size_t k = 0; k < kept.size(); ++k)
            {
                const auto& e = kept[(keptNext + k) % kept.size()];
                if (e.status != 0 && e.track == index)
                    played.push_back (e);
            }
        }
        if (played.empty())
            return nullptr;
        // The newest says which clock to go by: the launch clock of its run of the transport, or
        // the frames rendered.
        const auto last = played.back();
        auto time = [&] (const Kept& e) { return last.playing ? e.clock : e.live; };
        played.erase (std::remove_if (played.begin(), played.end(), [&] (const Kept& e)
        {
            return e.playing != last.playing || (last.playing && e.epoch != last.epoch);
        }), played.end());

        const auto sr = rate.load();
        const auto bar = 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * sr * std::max (1, beatsPerBar.load());
        // The last phrase: back to a rest of two bars.
        size_t first = played.size() - 1;
        while (first > 0 && (double) (time (played[first]) - time (played[first - 1])) < 2.0 * bar)
            --first;
        Frame start = -1, end = 0;
        for (size_t k = first; k < played.size(); ++k)
        {
            if (start < 0 && isNoteOn (played[k].status, played[k].data2))
                start = time (played[k]);
            end = std::max (end, time (played[k]));
        }
        if (start < 0)
            return nullptr;   // no note in it
        const auto origin = last.playing ? (Frame) std::llround (std::floor ((double) start / bar) * bar) : start;
        int bars = 1;
        while (bars < 32 && (double) (end - origin) >= bars * bar)
            bars *= 2;
        const auto length = (Frame) std::llround (bars * bar);

        auto clip = std::make_shared<MidiClip>();
        clip->bpm = tempo.load();
        clip->length = length;
        std::array<std::array<bool, 128>, 16> held {};
        for (size_t k = first; k < played.size(); ++k)
        {
            const auto& e = played[k];
            const auto at = std::clamp<Frame> (time (e) - origin, 0, length - 1);
            const auto ch = (size_t) (e.status & 0x0F), key = (size_t) (e.data1 & 0x7F);
            if (isNoteOn (e.status, e.data2))
                held[ch][key] = true;
            else if (isNoteOff (e.status, e.data2))
            {
                if (! held[ch][key])
                    continue;
                held[ch][key] = false;
            }
            clip->events.push_back ({ at, e.status, e.data1, e.data2 });
        }
        for (size_t ch = 0; ch < 16; ++ch)
            for (size_t key = 0; key < 128; ++key)
                if (held[ch][key])
                    clip->events.push_back ({ length - 1, (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
        std::stable_sort (clip->events.begin(), clip->events.end(), before);
        phase = last.playing ? origin : 0;
        return clip;
    }

    //==========================================================================
    // The audio thread, with the lock held.
    void Engine::resetClock()
    {
        clock = 0;
        ++epoch;
        for (auto& t : lanes)
        {
            t.launchedAt = 0;
            t.queuedAt = std::min<Frame> (t.queuedAt, 0);
        }
    }

    void Engine::report (int index, Track& t)
    {
        auto& k = t.take;
        std::unique_lock<std::mutex> q (liveLock, std::try_to_lock);
        if (! q.owns_lock())
            return;   // the next block tries again
        if (recordedOut.size() < recordedOut.capacity())
            recordedOut.push_back (k.done);
        if (k.replaced != nullptr && slotsOut.size() < slotsOut.capacity())
        {
            slotsOut.push_back (k.replaced);
            k.replaced = nullptr;
        }
        k.done = {};
        k.reporting = false;
        (void) index;
    }

    void Engine::recordNote (Track& t, Frame frame, const NoteEvent& e, Frame length)
    {
        auto& k = t.take;
        if (k.midi == nullptr)
            return;
        auto& clip = *k.midi;
        const bool overdub = k.state == RecordState::overdubbing;
        const auto grid = k.request.quantise * 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * rate.load();
        const auto ch = (size_t) (e.status & 0x0F), key = (size_t) (e.data1 & 0x7F);
        auto wrap = [&] (Frame f) { return overdub && length > 0 ? ((f % length) + length) % length : f; };
        if (isNoteOn (e.status, e.data2))
        {
            auto at = std::max<Frame> (0, roundTo (frame, grid));
            if (overdub)
                at = wrap (at);
            else if (length > 0 && at >= length)
                return;   // quantised past the end
            if (k.on[ch][key] >= 0)
                insertPlaced (clip, { std::max (at, k.on[ch][key] + 1), (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
            k.on[ch][key] = at;
            k.shift[ch][key] = at - (overdub ? wrap (frame) : frame);
            insertPlaced (clip, { at, e.status, e.data1, e.data2 });
            k.notes = true;
        }
        else if (isNoteOff (e.status, e.data2))
        {
            const auto began = k.on[ch][key];
            if (began < 0)
                return;   // its note began before the take
            k.on[ch][key] = -1;
            auto at = (overdub ? wrap (frame) : frame) + k.shift[ch][key];
            if (at <= began)
                at = overdub && length > 0 ? length - 1 : began + 1;   // held over the loop's end: it ends with the clip
            if (length > 0)
                at = std::min (at, length - 1);
            insertPlaced (clip, { std::max (at, began + 1), (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
        }
        else
            insertPlaced (clip, { std::clamp<Frame> (wrap (frame), 0, length > 0 ? length - 1 : frame), e.status, e.data1, e.data2 });
    }

    void Engine::finishSlotTake (int index, Track& t)
    {
        auto& k = t.take;
        const auto scene = k.request.scene;
        const auto length = std::max<Frame> (1, k.endAt - k.startAt);
        if (k.midi != nullptr)
        {
            // Notes still held end with the take.
            for (size_t ch = 0; ch < 16; ++ch)
                for (size_t key = 0; key < 128; ++key)
                    if (k.on[ch][key] >= 0)
                    {
                        insertPlaced (*k.midi, { std::max (length - 1, k.on[ch][key] + 1), (uint8_t) (0x80 | ch), (uint8_t) key, 0 });
                        k.on[ch][key] = -1;
                    }
            k.midi->length = length;
        }
        auto& slot = *k.slot;
        slot.clip = Clip();
        slot.clip.audio = k.audio;
        slot.clip.midi = k.midi;
        slot.clip.length = length;
        SlotPtr incoming = k.slot;
        std::swap (t.slots[(size_t) scene], incoming);
        k.replaced = incoming;   // the slot's clip from before, if it had one
        if (t.queuedScene == scene)
            t.queued = t.slots[(size_t) scene];
        if (k.request.playAfter)
        {
            // It plays on from this very frame.
            t.playing = t.slots[(size_t) scene];
            t.playingScene = scene;
            t.launchedAt = k.endAt;
            t.timeline = false;
            t.launchChanged = true;
        }
        else if (t.playingScene == scene)
        {
            t.playing = nullptr;
            t.playingScene = -1;
            t.launchChanged = true;
        }

        k.done = {};
        k.done.track = index;
        k.done.scene = scene;
        k.done.audio = k.audio;
        k.done.midi = k.midi;
        k.done.start = k.startAt;
        k.done.length = length;
        k.done.notes = k.notes;
        // The audio still on its way (the input's delay) is written before the UI keeps it.
        const auto latency = (Frame) std::llround (std::max (0.0f, inputLatencyMs.load()) * 0.001 * rate.load());
        if (k.audio != nullptr && latency > 0)
        {
            k.tailAudio = k.audio;
            k.tailStart = k.startAt;
            k.tailTo = k.endAt + latency;
            k.tailLength = length;
            k.tailChannels = strips[(size_t) index].inputChannels.load();
        }
        else
            k.reporting = true;

        // The next take, if one was asked for, starts on this frame.
        const auto endedAt = k.endAt;
        k.audio = nullptr;
        k.midi = nullptr;
        k.slot = nullptr;
        k.notes = false;
        k.stopAsked = false;
        if (k.nextScene >= 0)
        {
            if (k.length <= 0)
                k.length = std::min (endedAt - k.done.start, k.room);   // a take of no set length: the next is as long
            k.request.scene = k.nextScene;
            k.audio = std::move (k.nextAudio);
            k.midi = std::move (k.nextMidi);
            k.slot = std::move (k.nextSlot);
            k.nextScene = -1;
            k.startAt = endedAt;
            k.endAt = endedAt + k.length;
            k.state = RecordState::recording;
            recordShared[(size_t) index].scene = k.request.scene;
            recordShared[(size_t) index].start = k.startAt;
        }
        else
        {
            k.nextScene = -1;
            k.state = RecordState::none;
            k.endAt = -1;
        }
    }

    void Engine::recordBlock (Frame clockAt, Frame posAt, int frames, bool play)
    {
        const auto armedTrack = armed.load();
        const auto sr = rate.load();
        const auto latency = (Frame) std::llround (std::max (0.0f, inputLatencyMs.load()) * 0.001 * sr);
        const bool counting = countLeft.load() > 0;

        // The keyboard, kept for Capture.
        if (armedTrack >= 0)
            for (const auto& e : liveTaken)
            {
                kept[keptNext] = { liveClock, clockAt, epoch, play, armedTrack, e.status, e.data1, e.data2 };
                keptNext = (keptNext + 1) % kept.size();
            }

        for (int i = 0; i < maxTracks; ++i)
        {
            auto& t = lanes[(size_t) i];
            auto& k = t.take;
            auto& shared = recordShared[(size_t) i];
            const auto channels = strips[(size_t) i].inputChannels.load();
            auto inputAt = [&] (int f, bool right)
            {
                return channels == 1 ? inL[(size_t) f] : channels == 2 ? inR[(size_t) f] : right ? inR[(size_t) f] : inL[(size_t) f];
            };

            // A take that ended still gets its late audio.
            if (k.tailTo >= 0 && k.tailAudio != nullptr)
            {
                auto& a = *k.tailAudio;
                for (int f = 0; f < frames; ++f)
                {
                    const auto at = clockAt - k.tailStart + f - latency;
                    if (at >= 0 && at < k.tailLength && at < a.length())
                    {
                        a.left[(size_t) at] = inputAt (f, false);
                        a.right[(size_t) at] = inputAt (f, true);
                    }
                }
                if (clockAt + frames >= k.tailTo || ! play)
                {
                    k.tailTo = -1;
                    k.tailAudio = nullptr;
                    k.reporting = true;
                }
            }
            if (k.reporting)
                report (i, t);
            if (k.state == RecordState::none)
            {
                shared.state.store (0, std::memory_order_relaxed);
                continue;
            }
            const bool keys = i == armedTrack && k.request.midi;

            if (k.timeline)
            {
                auto mark = [&] (Mark::Kind kind, Frame frame, uint8_t status = 0, uint8_t data1 = 0, uint8_t data2 = 0)
                {
                    // marks has room reserved; past it, a mark is lost rather than allocated
                    if (marks.size() < marks.capacity())
                        marks.push_back ({ kind, i, frame, (int64_t) streamSent, status, data1, data2 });
                };
                std::unique_lock<std::mutex> q (liveLock, std::try_to_lock);
                if (! q.owns_lock())
                    continue;   // this block is left out of the take; rare
                if (k.state == RecordState::waiting)
                {
                    if (! play)
                        continue;
                    k.state = RecordState::recording;
                    k.startAt = k.lastPos = posAt;
                    shared.start.store (posAt, std::memory_order_relaxed);
                    streamSent = 0;
                    mark (Mark::pass, posAt);
                }
                if (! play || k.stopAsked || (k.length > 0 && (Frame) streamSent >= k.length))
                {
                    mark (Mark::end, posAt);
                    k.state = RecordState::none;
                    shared.state.store (0, std::memory_order_relaxed);
                    continue;
                }
                if (posAt != k.lastPos)
                {
                    ++k.pass;   // the transport jumped (a loop): a new pass
                    mark (Mark::pass, posAt);
                }
                if (keys)
                    for (const auto& e : liveTaken)
                        mark (Mark::note, posAt, e.status, e.data1, e.data2);
                if (k.request.audio && ! streamRing.empty())
                {
                    const auto ringFrames = streamRing.size() / 2;
                    const auto w = streamWritten.load (std::memory_order_relaxed);
                    if (w + (uint64_t) frames - streamRead.load (std::memory_order_acquire) <= ringFrames)
                    {
                        for (int f = 0; f < frames; ++f)
                        {
                            const auto at = (size_t) ((w + (uint64_t) f) % ringFrames) * 2;
                            streamRing[at] = inL[(size_t) f];   // both sides as they came: the UI picks the channels
                            streamRing[at + 1] = inR[(size_t) f];
                        }
                        streamWritten.store (w + (uint64_t) frames, std::memory_order_release);
                    }
                }
                streamSent += (uint64_t) frames;
                k.lastPos = posAt + frames;
                shared.state.store ((int) k.state, std::memory_order_relaxed);
                shared.recorded.store ((Frame) streamSent, std::memory_order_relaxed);
                shared.progress.store (k.length > 0 ? std::min (1.0f, (float) streamSent / (float) k.length) : 0.0f, std::memory_order_relaxed);
                continue;
            }

            if (k.state == RecordState::overdubbing)
            {
                const auto len = t.playing != nullptr ? t.playing->clip.length : 0;
                const bool same = t.playing != nullptr && t.playingScene == k.request.scene
                               && (t.playing->clip.audio == k.audio || t.playing->clip.midi == k.midi);
                if (! same || len <= 0 || k.stopAsked || ! play)
                {
                    // Kept as it is now.
                    k.done = {};
                    k.done.track = i;
                    k.done.scene = k.request.scene;
                    k.done.overdub = true;
                    k.done.audio = k.audio;
                    k.done.midi = k.midi;
                    k.done.length = len;
                    k.done.notes = k.notes;
                    k.reporting = true;
                    k.state = RecordState::none;
                    k.audio = nullptr;
                    k.midi = nullptr;
                    shared.state.store (0, std::memory_order_relaxed);
                    report (i, t);
                    continue;
                }
                const auto p = ((clockAt - t.launchedAt) % len + len) % len;
                if (k.audio != nullptr)
                {
                    auto& a = *k.audio;
                    const auto offset = t.playing->clip.offset;
                    for (int f = 0; f < frames; ++f)
                    {
                        const auto at = (size_t) ((((p + f - latency) % len) + len) % len + offset);
                        if (at < a.left.size())
                        {
                            a.left[at] += inputAt (f, false);
                            a.right[at] += inputAt (f, true);
                        }
                    }
                }
                if (keys)
                    for (const auto& e : liveTaken)
                        recordNote (t, p, e, len);
                shared.state.store ((int) k.state, std::memory_order_relaxed);
                shared.progress.store ((float) p / (float) len, std::memory_order_relaxed);
                continue;
            }

            // A slot's take: waiting for its first frame, or recording.
            if (k.state == RecordState::waiting)
            {
                const auto start = k.startAt >= 0 ? k.startAt : counting ? 0 : -1;
                if (start < 0 && ! play)
                    continue;
                if (! play || clockAt < start)
                {
                    // A note played a little early for the first beat goes on it.
                    if (keys && start - clockAt <= (Frame) (earlySeconds * sr))
                        for (const auto& e : liveTaken)
                            recordNote (t, clockAt - start, e, k.room);   // the count-in runs up to 0
                    continue;
                }
                if (k.startAt < 0)
                    k.startAt = clockAt;
                if (clockAt < k.startAt)
                    continue;
                k.state = RecordState::recording;
                shared.start.store (k.startAt, std::memory_order_relaxed);
                if (k.length > 0)
                    k.endAt = k.startAt + k.length;
                // The take takes the place of what the track's launcher played.
                if (t.playing != nullptr)
                {
                    t.playing = nullptr;
                    t.playingScene = -1;
                    t.launchChanged = true;
                }
            }
            if (! play)
            {
                // The transport stopped before the take ended: nothing kept.
                k.state = RecordState::none;
                k.nextScene = -1;
                shared.state.store (0, std::memory_order_relaxed);
                continue;
            }
            const auto quantum = launchQuantise.load() * 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * sr;
            const auto at = clockAt - k.startAt;
            if (k.endAt < 0 && k.stopAsked)
            {
                // Ends at the next quantise: its length whole steps of it, one at least.
                const auto steps = quantum >= 1.0 ? std::max (1.0, std::ceil ((double) at / quantum - 1.0e-9)) : 0.0;
                k.endAt = k.startAt + (quantum >= 1.0 ? (Frame) std::llround (steps * quantum) : std::max<Frame> (1, at));
                k.endAt = std::min (k.endAt, k.startAt + k.room);
            }
            if (k.endAt < 0 && at + frames >= k.room)
            {
                // Out of room: as many whole steps as fit.
                const auto steps = quantum >= 1.0 ? std::floor ((double) k.room / quantum) : 0.0;
                k.endAt = k.startAt + (steps >= 1.0 ? (Frame) std::llround (steps * quantum) : k.room);
            }
            const auto lengthNow = k.endAt >= 0 ? k.endAt - k.startAt : k.room;
            if (keys)
                for (const auto& e : liveTaken)
                    recordNote (t, at, e, lengthNow);
            if (k.audio != nullptr)
            {
                auto& a = *k.audio;
                for (int f = 0; f < frames; ++f)
                {
                    const auto w = at + f - latency;
                    if (w >= 0 && w < lengthNow && w < a.length())
                    {
                        a.left[(size_t) w] = inputAt (f, false);
                        a.right[(size_t) w] = inputAt (f, true);
                    }
                }
            }
            shared.state.store ((int) k.state, std::memory_order_relaxed);
            shared.recorded.store (at + frames, std::memory_order_relaxed);
            shared.length.store (k.endAt >= 0 ? k.endAt - k.startAt : k.length, std::memory_order_relaxed);
            shared.progress.store (k.endAt >= 0 ? std::min (1.0f, (float) (at + frames) / (float) std::max<Frame> (1, k.endAt - k.startAt)) : 0.0f,
                                   std::memory_order_relaxed);
            if (k.endAt >= 0 && clockAt + frames >= k.endAt)
            {
                finishSlotTake (i, t);
                if (k.reporting)
                    report (i, t);
                shared.state.store ((int) k.state, std::memory_order_relaxed);
            }
        }
    }

    void Engine::click (float* left, float* right, int frames, Frame from, double beatFrames)
    {
        // A short sine with a quick decay on each beat: the first of the bar higher.
        const auto sr = (float) rate.load();
        const auto clickFrames = (int) (0.03f * sr);
        const auto gain = std::pow (10.0f, metronomeDb.load() / 20.0f);
        const auto bpb = std::max (1, beatsPerBar.load());
        for (int f = 0; f < frames; ++f)
        {
            const auto x = from + f;
            if (beatFrames >= 1.0)
            {
                const auto beat = (int64_t) std::llround ((double) x / beatFrames);
                if ((Frame) std::llround ((double) beat * beatFrames) == x)
                {
                    clickAt = 0;
                    clickPhase = 0.0f;
                    clickHz = beat % bpb == 0 ? 1760.0f : 1175.0f;
                    clickGain = gain * (beat % bpb == 0 ? 1.0f : 0.7f);
                }
            }
            if (clickAt < 0)
                continue;
            const auto t = (float) clickAt / sr;
            const auto v = std::sin (clickPhase) * std::exp (-t / 0.008f) * clickGain;
            left[f] += v;
            right[f] += v;
            clickPhase += 2.0f * 3.14159265f * clickHz / sr;
            if (++clickAt >= clickFrames)
                clickAt = -1;
        }
    }
}
