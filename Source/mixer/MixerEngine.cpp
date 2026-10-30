// SPDX-License-Identifier: AGPL-3.0-only
#include "MixerEngine.h"
#include "../core/Dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

        // The same, in place.
        void scaleRamped (float* io, int frames, float from, float to)
        {
            const auto step = (to - from) / (float) frames;
            for (int i = 0; i < frames; ++i)
                io[i] *= from + step * (float) (i + 1);
        }
    }

    //==========================================================================
    void Engine::Track::Biquad::process (float* l, float* r, int frames)
    {
        float* sides[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            auto* x = sides[c];
            auto z1 = z[c][0], z2 = z[c][1];
            for (int i = 0; i < frames; ++i)
            {
                const auto in = x[i];
                const auto out = b0 * in + z1;
                z1 = b1 * in - a1 * out + z2;
                z2 = b2 * in - a2 * out;
                x[i] = out;
            }
            // Denormals and a filter left ringing on nothing settle to silence.
            z[c][0] = std::abs (z1) < 1.0e-20f ? 0.0f : z1;
            z[c][1] = std::abs (z2) < 1.0e-20f ? 0.0f : z2;
        }
    }

    // Butterworth: one stage at Q 0.707 for 12 dB an octave, two at 0.541 and 1.307 for 24.
    void Engine::Track::Cut::update (bool turnOn, float frequency, int dbPerOctave, bool high, double sampleRate)
    {
        frequency = std::clamp (frequency, minCutHz, std::min (maxCutHz, (float) (sampleRate * 0.45)));
        dbPerOctave = dbPerOctave >= 24 ? 24 : 12;
        if (turnOn != on)
            for (auto& b : stages)
                std::fill (&b.z[0][0], &b.z[0][0] + 4, 0.0f);
        on = turnOn;
        if (! on || (std::abs (frequency - hz) < 1.0e-3f && dbPerOctave == slope))
            return;
        hz = frequency;
        slope = dbPerOctave;
        const double qs12[] = { 0.70710678 }, qs24[] = { 0.54119610, 1.30656296 };
        const auto* qs = slope == 24 ? qs24 : qs12;
        const int count = slope == 24 ? 2 : 1;
        const auto w = 2.0 * 3.14159265358979 * hz / sampleRate;
        for (int k = 0; k < 2; ++k)
        {
            auto& b = stages[(size_t) k];
            if (k >= count)
            {
                b.b0 = 1.0f;
                b.b1 = b.b2 = b.a1 = b.a2 = 0.0f;
                continue;
            }
            const auto alpha = std::sin (w) / (2.0 * qs[k]);
            const auto cw = std::cos (w);
            const auto a0 = 1.0 + alpha;
            const auto b1 = high ? 1.0 - cw : -(1.0 + cw);
            b.b0 = (float) (std::abs (b1) * 0.5 / a0);
            b.b1 = (float) (b1 / a0);
            b.b2 = b.b0;
            b.a1 = (float) (-2.0 * cw / a0);
            b.a2 = (float) ((1.0 - alpha) / a0);
        }
    }

    void Engine::Track::Cut::process (float* l, float* r, int frames)
    {
        if (! on)
            return;
        stages[0].process (l, r, frames);
        if (slope == 24)
            stages[1].process (l, r, frames);
    }

    namespace
    {
        // Where a surround file's speaker is (as ambi::layout has them): 5.x's surrounds at
        // ±110 degrees, 7.x's sides at ±90 with the backs at ±150, the heights at 45 up.
        AudioClip::BedSpeaker direction (dsp::Speaker s, bool backs, bool quad)
        {
            using S = dsp::Speaker;
            switch (s)
            {
                case S::left:           return { -30.0f, 0.0f };
                case S::right:          return { 30.0f, 0.0f };
                case S::lfe:
                case S::lfe2:           return { 0.0f, 0.0f, true };
                case S::sideLeft:       return { backs ? -90.0f : -110.0f, 0.0f };
                case S::sideRight:      return { backs ? 90.0f : 110.0f, 0.0f };
                case S::rearLeft:       return { quad ? -135.0f : -150.0f, 0.0f };
                case S::rearRight:      return { quad ? 135.0f : 150.0f, 0.0f };
                case S::rearCentre:     return { 180.0f, 0.0f };
                case S::wideLeft:       return { -60.0f, 0.0f };
                case S::wideRight:      return { 60.0f, 0.0f };
                case S::topFrontLeft:   return { -45.0f, 45.0f };
                case S::topFrontRight:  return { 45.0f, 45.0f };
                case S::topMiddleLeft:  return { -90.0f, 45.0f };
                case S::topMiddleRight: return { 90.0f, 45.0f };
                case S::topRearLeft:    return { -135.0f, 45.0f };
                case S::topRearRight:   return { 135.0f, 45.0f };
                default:                return { 0.0f, 0.0f };
            }
        }

        // Reads every channel of `data` in parts, handing each part to `use` (channel pointers, offset, count).
        template <typename Use>
        void readInParts (const AudioData& data, Use&& use)
        {
            const auto n = (size_t) data.length();
            std::vector<std::vector<float>> scratch ((size_t) data.numChannels(), std::vector<float> (65536));
            std::vector<float*> ptrs;
            for (auto& c : scratch)
                ptrs.push_back (c.data());
            for (size_t at = 0; at < n; at += 65536)
            {
                const auto count = (int) std::min<size_t> (65536, n - at);
                data.read ((Frame) at, count, ptrs.data());
                use (ptrs, at, count);
            }
        }
    }

    AudioClipPtr makeAudioClip (const AudioData& data, const std::string& path, const std::string& layoutName)
    {
        auto clip = std::make_shared<AudioClip>();
        clip->path = path;
        const auto n = (size_t) data.length();
        const auto numChannels = data.numChannels();
        clip->fileChannels = numChannels;
        clip->left.resize (n);
        clip->right.resize (n);
        clip->mono = numChannels < 2;
        if (clip->mono)
        {
            float* one[1] = { clip->left.data() };
            data.read (0, (int) std::min<size_t> (n, (size_t) INT32_MAX), one);
            std::copy (clip->left.begin(), clip->left.end(), clip->right.begin());
            return clip;
        }
        if (numChannels == 2)
        {
            float* dest[2] = { clip->left.data(), clip->right.data() };
            readInParts (data, [&] (const std::vector<float*>& from, size_t at, int count)
            {
                for (int ch = 0; ch < 2; ++ch)
                    std::copy (from[(size_t) ch], from[(size_t) ch] + count, dest[ch] + at);
            });
            return clip;
        }

        // More than two: the stereo fold-down, and every channel when the layout says where they go.
        const auto layout = dsp::speakerLayout (numChannels, layoutName);
        clip->layout = layout.name;
        const auto order = dsp::ambisonicOrder (layout.name);
        bool placeable = numChannels <= spatial::channels;
        if (order > 0)
            clip->ambisonicOrder = order;
        else
        {
            const bool backs = std::find (layout.speakers.begin(), layout.speakers.end(), dsp::Speaker::rearLeft) != layout.speakers.end()
                            && std::find (layout.speakers.begin(), layout.speakers.end(), dsp::Speaker::sideLeft) != layout.speakers.end();
            for (const auto sp : layout.speakers)
            {
                placeable = placeable && sp != dsp::Speaker::other && sp != dsp::Speaker::mono;
                clip->speakers.push_back (direction (sp, backs, numChannels == 4));
            }
            placeable = placeable && layout.speakers.size() >= 2 && layout.speakers[0] == dsp::Speaker::left && layout.speakers[1] == dsp::Speaker::right;
        }
        if (! placeable)
        {
            clip->speakers.clear();
            clip->ambisonicOrder = 0;
        }
        else
            clip->channels.assign ((size_t) numChannels, std::vector<float> (n));
        const auto fold = dsp::stereoDownmix (numChannels, layout.name);
        readInParts (data, [&] (const std::vector<float*>& from, size_t at, int count)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* in = from[(size_t) ch];
                if (! clip->channels.empty())
                    std::copy (in, in + count, clip->channels[(size_t) ch].begin() + (std::ptrdiff_t) at);
                const auto gl = fold[0][(size_t) ch], gr = fold[1][(size_t) ch];
                if (gl == 0.0f && gr == 0.0f)
                    continue;
                for (int k = 0; k < count; ++k)
                {
                    clip->left[at + (size_t) k] += in[k] * gl;
                    clip->right[at + (size_t) k] += in[k] * gr;
                }
            }
        });
        return clip;
    }

    std::string clipFormat (const AudioClip& a)
    {
        if (a.mono)
            return "Mono";
        if (a.fileChannels <= 2)
            return "Stereo";
        if (a.ambisonicOrder > 0)
        {
            static const char* orders[] { "", "AmbiX 1st", "AmbiX 2nd", "AmbiX 3rd" };
            return orders[std::clamp (a.ambisonicOrder, 1, 3)];
        }
        if (a.surround())
            return a.layout;
        return std::to_string (a.fileChannels) + " ch";
    }

    std::vector<AudioClipPtr> splitChannels (const AudioData& data, const std::string& path)
    {
        const auto n = (size_t) data.length();
        std::vector<std::shared_ptr<AudioClip>> clips;
        for (int ch = 0; ch < data.numChannels(); ++ch)
        {
            auto clip = std::make_shared<AudioClip>();
            clip->path = path;
            clip->mono = true;
            clip->fileChannels = 1;
            clip->left.resize (n);
            clips.push_back (clip);
        }
        readInParts (data, [&] (const std::vector<float*>& from, size_t at, int count)
        {
            for (size_t ch = 0; ch < clips.size(); ++ch)
                std::copy (from[ch], from[ch] + count, clips[ch]->left.begin() + (std::ptrdiff_t) at);
        });
        std::vector<AudioClipPtr> out;
        for (auto& c : clips)
        {
            c->right = c->left;
            out.push_back (c);
        }
        return out;
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
        for (auto* v : { &mixL, &mixR, &trackL, &trackR, &instL, &instR, &monoBuf, &outL, &outR, &sendPreL, &sendPreR, &audL, &audR, &dryL, &dryR, &duckGains,
                         &inL, &inR, &busL, &busR, &lfeBus, &lfeScratch, &bedBuf })
            v->resize (block);
        inputFifo.resize (2 * 16384);
        inScratch.resize (2 * 4096);
        recordedOut.reserve (64);
        slotsOut.reserve (64);
        marks.reserve (16384);
        for (auto& k : keyLevels)
            k.resize (block);
        for (int r = 0; r < numReturns; ++r)
        {
            returnL[(size_t) r].resize (block);
            returnR[(size_t) r].resize (block);
        }
        for (int c = 0; c < spatial::channels; ++c)
        {
            scene[(size_t) c].resize (block);
            sceneAt[(size_t) c] = scene[(size_t) c].data();
        }
        for (int l = 0; l < spatial::maxSpeakers; ++l)
        {
            feeds[(size_t) l].resize (block);
            feedAt[(size_t) l] = feeds[(size_t) l].data();
        }
        speakerQueue.resize ((size_t) queueFrames * spatial::maxSpeakers);
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
        {
            if (t.instrument != nullptr)
                t.instrument->prepare (rate.load(), block);
            for (auto& x : t.inserts)
                x.effect->prepare (rate.load(), block);
            t.lowCut.hz = t.highCut.hz = 0.0f;   // worked out again for the new rate
        }
        for (auto& fx : returnEffects)
            if (fx != nullptr)
                fx->prepare (rate.load(), block);
        for (auto& x : busInserts)
            x.effect->prepare (rate.load(), block);
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
        t.mono = monoClips (t.clips);
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

    void Engine::setInserts (int index, Inserts list)
    {
        if (index < 0 || index > busTrack)
            return;
        list.erase (std::remove_if (list.begin(), list.end(), [] (const Insert& x) { return x.effect == nullptr; }), list.end());
        const auto most = index == busTrack ? maxBusInserts : maxInserts;
        if ((int) list.size() > most)
            list.resize ((size_t) most);
        // Effects new to the track are prepared before it hears them.
        const auto old = inserts (index);
        for (auto& x : list)
            if (std::none_of (old.begin(), old.end(), [&] (const Insert& o) { return o.effect == x.effect; }))
                x.effect->prepare (rate.load(), block);
        const std::lock_guard<std::mutex> l (lock);
        std::swap (index == busTrack ? busInserts : lanes[(size_t) index].inserts, list);
    }   // the effects taken out are let go here, after the lock

    void Engine::setReturnEffect (int index, EffectPtr effect)
    {
        if (index < 0 || index >= numReturns)
            return;
        if (effect != nullptr)
            effect->prepare (rate.load(), block);
        const std::lock_guard<std::mutex> l (lock);
        std::swap (returnEffects[(size_t) index], effect);
    }

    EffectPtr Engine::returnEffect (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return index >= 0 && index < numReturns ? returnEffects[(size_t) index] : nullptr;
    }

    // The two layers into one: the audio's stereo part into instL and instR, its mono part
    // left in monoBuf, each at its layer's level, one ducked by the other in a hybrid track.
    void Engine::layers (int index, Track& t, const Strip& s, TrackMode mode, bool hasMono, int frames, bool jumped)
    {
        const bool hybrid = mode == TrackMode::hybrid;
        const auto audioGain = hybrid ? dbToGain (s.audioLayerDb.load()) : 1.0f;
        const auto instGain = mode == TrackMode::audio ? 0.0f : hybrid ? dbToGain (s.instrumentLayerDb.load()) : 1.0f;
        if (jumped)
        {
            t.audioLayer = audioGain;
            t.instrumentLayer = instGain;
        }
        if (std::abs (t.audioLayer - 1.0f) > 1.0e-6f || std::abs (audioGain - 1.0f) > 1.0e-6f)
        {
            scaleRamped (audL.data(), frames, t.audioLayer, audioGain);
            scaleRamped (audR.data(), frames, t.audioLayer, audioGain);
            if (hasMono)
                scaleRamped (monoBuf.data(), frames, t.audioLayer, audioGain);
        }
        if (std::abs (t.instrumentLayer - 1.0f) > 1.0e-6f || std::abs (instGain - 1.0f) > 1.0e-6f)
        {
            scaleRamped (instL.data(), frames, t.instrumentLayer, instGain);
            scaleRamped (instR.data(), frames, t.instrumentLayer, instGain);
        }
        t.audioLayer = audioGain;
        t.instrumentLayer = instGain;

        // The duck: a peak follower on the key (the other layer of a hybrid track, or another
        // track's sound); at -6 dBFS and above the key pushes the rest down by the whole amount,
        // below that by its share. With a frequency, only what lies below it is ducked.
        const auto key = std::clamp (s.duckKey.load(), -1, maxTracks - 1);
        const auto amount = (key >= 0 || hybrid) ? std::clamp (s.duckDb.load(), 0.0f, 48.0f) : 0.0f;
        if (amount > 0.01f && key != index)
        {
            const bool audioLeads = key < 0 && s.audioDucksInstrument.load();
            const auto r = (float) rate.load();
            const auto attack = 1.0f - std::exp (-1.0f / (std::max (0.05f, s.duckAttackMs.load()) * 0.001f * r));
            const auto release = 1.0f - std::exp (-1.0f / (std::max (1.0f, s.duckReleaseMs.load()) * 0.001f * r));
            auto& gains = duckGains;
            for (int k = 0; k < frames; ++k)
            {
                const auto level = key >= 0 ? keyLevels[(size_t) key][(size_t) k]
                                 : audioLeads ? std::max ({ std::abs (audL[(size_t) k]), std::abs (audR[(size_t) k]), std::abs (monoBuf[(size_t) k]) })
                                              : std::max (std::abs (instL[(size_t) k]), std::abs (instR[(size_t) k]));
                auto& e = t.duckEnvelope;
                e += (level - e) * (level > e ? attack : release);
                gains[(size_t) k] = std::pow (10.0f, -amount * std::min (1.0f, e / 0.5f) / 20.0f);
            }
            // What is ducked: the audio, the instrument, or (keyed by another track) both.
            const bool duckAudio = key >= 0 || ! audioLeads, duckInstrument = key >= 0 || audioLeads;
            const auto below = s.duckBelowHz.load();
            t.duckSplit.update (below > 0.0f, below, 12, true, r);   // a high cut: the low-pass
            std::array<float*, 5> targets { duckInstrument ? instL.data() : nullptr, duckInstrument ? instR.data() : nullptr,
                                            duckAudio ? audL.data() : nullptr, duckAudio ? audR.data() : nullptr,
                                            duckAudio && hasMono ? monoBuf.data() : nullptr };
            for (size_t c = 0; c < targets.size(); ++c)
            {
                auto* x = targets[c];
                if (x == nullptr)
                    continue;
                if (! t.duckSplit.on)
                {
                    for (int k = 0; k < frames; ++k)
                        x[k] *= gains[(size_t) k];
                    continue;
                }
                // The low part (a low-pass of the high cut's kind) ducked, the rest kept: they add back to x.
                auto& b = t.duckSplit.stages[0];
                auto z1 = t.duckState[c][0], z2 = t.duckState[c][1];
                for (int k = 0; k < frames; ++k)
                {
                    const auto in = x[k];
                    const auto low = b.b0 * in + z1;
                    z1 = b.b1 * in - b.a1 * low + z2;
                    z2 = b.b2 * in - b.a2 * low;
                    x[k] = in - low + low * gains[(size_t) k];
                }
                t.duckState[c][0] = std::abs (z1) < 1.0e-20f ? 0.0f : z1;
                t.duckState[c][1] = std::abs (z2) < 1.0e-20f ? 0.0f : z2;
            }
        }
        else
            t.duckEnvelope = 0.0f;

        for (int k = 0; k < frames; ++k)
        {
            instL[(size_t) k] += audL[(size_t) k];
            instR[(size_t) k] += audR[(size_t) k];
        }
    }

    Inserts Engine::inserts (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return index == busTrack ? busInserts : lanes[(size_t) index].inserts;
    }

    void Engine::setDecoder (std::shared_ptr<spatial::Decoder> d)
    {
        const std::lock_guard<std::mutex> l (lock);
        std::swap (spatialDecoder, d);
    }   // the old decoder is let go here, after the lock

    std::shared_ptr<spatial::Decoder> Engine::decoder() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return spatialDecoder;
    }

    void Engine::place (const float* left, const float* right, int frames, const Panner3d& panner, spatial::Gains& last, bool jumped)
    {
        const auto now = spatial::gains (panner.place());
        spatial::encode (left, right, frames, jumped ? now : last, now, sceneAt.data());
        last = now;
    }

    void Engine::placeBeds (Track& t, const Clips& clips, Frame base, int frames, const spatial::Place& now, float level, bool jumped)
    {
        const auto was = jumped ? now : t.bedPlace;
        const auto levelWas = jumped ? level : t.bedLevel;
        t.bedPlace = now;
        t.bedLevel = level;
        if (level <= 0.0f && levelWas <= 0.0f)
            return;
        const auto step = (level - levelWas) / (float) frames;
        // One channel of a clip, with its edge fades, into bedBuf.
        const auto read = [&] (const Clip& c, const std::vector<float>& channel)
        {
            std::fill (bedBuf.begin(), bedBuf.begin() + frames, 0.0f);
            const auto from = std::max (base, c.start), to = std::min (base + frames, c.end());
            const auto fade = std::max<Frame> (1, std::min<Frame> (96, c.length / 4));
            const auto last = c.audio->length() - 1;
            for (auto f = from; f < to; ++f)
            {
                const auto forward = f - c.start + c.offset;
                const auto src = (size_t) (c.reversed ? last - forward : forward);
                const auto edge = std::min (f - c.start, c.end() - 1 - f);
                bedBuf[(size_t) (f - base)] = channel[src] * (edge < fade ? (float) (edge + 1) / (float) (fade + 1) : 1.0f);
            }
        };
        for (const auto& c : clips)
        {
            if (c.audio == nullptr || c.muted || ! c.audio->surround() || c.start >= base + frames || c.end() <= base)
                continue;
            const auto& a = *c.audio;
            if (a.ambisonicOrder > 0)
            {
                // AmbiX as it is, turned by the panner's azimuth and elevation (made again only when they move).
                if (std::abs (now.azimuth - t.turnAzimuth) > 1.0e-3f || std::abs (now.elevation - t.turnElevation) > 1.0e-3f)
                {
                    const auto m = ambi::rotation (spatial::order, now.azimuth, now.elevation, 0.0);
                    for (int r = 0; r < spatial::channels; ++r)
                        for (int k = 0; k < spatial::channels; ++k)
                            t.turn[(size_t) r][(size_t) k] = (float) m[(size_t) r][(size_t) k];
                    t.turnAzimuth = now.azimuth;
                    t.turnElevation = now.elevation;
                }
                const auto count = std::min ((int) a.channels.size(), spatial::channels);
                for (int k = 0; k < count; ++k)
                {
                    read (c, a.channels[(size_t) k]);
                    for (int f = 0; f < frames; ++f)
                        bedBuf[(size_t) f] *= levelWas + step * (float) (f + 1);
                    spatial::PointGains g {};
                    for (int r = 0; r < spatial::channels; ++r)
                        g[(size_t) r] = t.turn[(size_t) r][(size_t) k];
                    spatial::encodeOne (bedBuf.data(), frames, g, g, sceneAt.data());
                }
                continue;
            }
            // A surround file's speakers past the front pair, where they are turned by the panner's
            // azimuth and at its distance; its LFE to the subwoofers.
            for (size_t k = 2; k < a.channels.size() && k < a.speakers.size(); ++k)
            {
                const auto& sp = a.speakers[k];
                read (c, a.channels[k]);
                if (sp.lfe)
                {
                    addRamped (lfeBus.data(), bedBuf.data(), frames, levelWas, level);
                    continue;
                }
                auto from = spatial::pointGains (sp.azimuth + was.azimuth, sp.elevation, was.distance);
                auto to = spatial::pointGains (sp.azimuth + now.azimuth, sp.elevation, now.distance);
                for (int r = 0; r < spatial::channels; ++r)
                {
                    from[(size_t) r] *= levelWas;
                    to[(size_t) r] *= level;
                }
                spatial::encodeOne (bedBuf.data(), frames, from, to, sceneAt.data());
            }
        }
    }

    void Engine::feedSpeakers (int frames)
    {
        const auto count = spatialDecoder->speakerCount();
        if (count != queueChannels || spatialDecoder->setup().firstOutput != queueFirst)
        {
            queueChannels = count;
            queueFirst = spatialDecoder->setup().firstOutput;
            queued = taken = 0;
        }
        if (count == 0)
            return;
        const auto master = dbToGain (masterDb.load());
        for (int k = 0; k < frames; ++k)
        {
            const auto g = speakerGain + (master - speakerGain) * (float) (k + 1) / (float) frames;
            auto* frame = speakerQueue.data() + (size_t) ((queued + (uint64_t) k) % queueFrames) * (size_t) count;
            for (int l = 0; l < count; ++l)
                frame[l] = feeds[(size_t) l][(size_t) k] * g;
        }
        // The master's per-speaker meter: the peak since the UI last took it.
        for (int l = 0; l < count; ++l)
        {
            float peak = 0.0f;
            for (int k = 0; k < frames; ++k)
                peak = std::max (peak, std::abs (feeds[(size_t) l][(size_t) k]));
            peak *= master;
            if (peak > speakerPeaks[(size_t) l].load (std::memory_order_relaxed))
                speakerPeaks[(size_t) l].store (peak, std::memory_order_relaxed);
        }
        speakerGain = master;
        queued += (uint64_t) frames;
        if (queued - taken > (uint64_t) queueFrames)
            taken = queued - (uint64_t) queueFrames;   // nobody is taking them: keep the newest
    }

    void Engine::takeSpeakers (float* const* outs, int numOuts, int frames)
    {
        const auto count = queueChannels;
        if (count == 0 || ! spatialOn.load())
        {
            taken = queued;
            return;
        }
        // Behind by more than a few blocks (a callback skipped): catch up to the newest.
        if (queued - taken > (uint64_t) (frames + 4 * block))
            taken = queued - (uint64_t) frames;
        const auto have = (int) std::min<uint64_t> (queued - taken, (uint64_t) frames);
        for (int k = 0; k < have; ++k)
        {
            const auto* frame = speakerQueue.data() + (size_t) ((taken + (uint64_t) k) % queueFrames) * (size_t) count;
            for (int l = 0; l < count; ++l)
                if (const auto ch = queueFirst + l; ch >= 0 && ch < numOuts)
                    outs[ch][k] += frame[l];
        }
        taken += (uint64_t) have;
    }

    std::vector<float> Engine::takeSpeakerPeaks()
    {
        std::vector<float> peaks;
        const auto d = decoder();
        const auto count = d != nullptr && spatialOn.load() ? d->speakerCount() : 0;
        for (int l = 0; l < count; ++l)
            peaks.push_back (speakerPeaks[(size_t) l].exchange (0.0f));
        return peaks;
    }

    bool Engine::anyToBus() const
    {
        for (int i = 0; i < tracks.load(); ++i)
            if (strips[(size_t) i].toBus.load())
                return true;
        return false;
    }

    bool Engine::monoClips (const Clips& clips)
    {
        bool any = false;
        for (const auto& c : clips)
        {
            if (c.audio == nullptr || ! c.audio->mono)
                return false;
            any = true;
        }
        return any;
    }

    bool Engine::monoTrack (int index) const
    {
        const std::lock_guard<std::mutex> l (lock);
        return lanes[(size_t) index].mono;
    }

    Frame Engine::delayFrames (int index) const
    {
        const auto ms = std::clamp (strips[(size_t) index].delayMs.load(), -maxDelayMs, maxDelayMs);
        return (Frame) std::llround (ms * 0.001 * rate.load());
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
        countLeft = 0;
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

    void Engine::launchAt (int track, int scene, Frame clockFrame)
    {
        if (track < 0 || track >= maxTracks || scene < -1 || scene >= maxScenes)
            return;
        if (! playing.load())
        {
            launch (track, scene);
            return;
        }
        const std::lock_guard<std::mutex> l (liveLock);
        if (launchQueue.size() < 256)
            launchQueue.push_back ({ track, scene, false, false, std::max<Frame> (0, clockFrame) });
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
                t.queuedAt = r.at >= 0 && play ? r.at : play ? quantiseFrom (launchQuantise.load()) : now;
                continue;
            }
            t.queued = s;
            t.queuedScene = r.scene;
            t.queuedAt = r.at >= 0 && play ? r.at
                       : r.now || ! play ? now : quantiseFrom (s->quantise < 0.0 ? launchQuantise.load() : s->quantise);
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
                t.launchedAt = t.queuedAt;   // now, or a launchAt's frame gone by
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
        return end > 0 ? std::max<Frame> (0, end + delayFrames (i)) : 0;
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

    void Engine::liveMidi (uint8_t status, uint8_t data1, uint8_t data2, bool heard)
    {
        {
            const std::lock_guard<std::mutex> l (liveLock);
            if (liveQueue.size() < 1024)
                liveQueue.push_back ({ 0, status, data1, data2 });
        }
        if (heard)
        {
            const std::lock_guard<std::mutex> l (listenerLock);
            if (liveListener)
                liveListener (status, data1, data2);
        }
    }

    void Engine::setLiveListener (std::function<void (uint8_t, uint8_t, uint8_t)> f)
    {
        const std::lock_guard<std::mutex> l (listenerLock);
        liveListener = std::move (f);
    }

    void Engine::panic() { panicPending = true; }

    Meter Engine::takeMeter (int index)
    {
        Meter m;
        if (index < 0 || index >= (int) meters.size())
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
            resetClock();

        // The audio input that has come in, kept until its blocks use it; a little at most beyond
        // this call, so its delay stays the same from take to take.
        if (auto* in = input.load())
        {
            const auto capacity = inputFifo.size() / 2;
            for (int got = 1; got > 0 && inputHave < capacity;)
            {
                got = in->read (inScratch.data(), (int) std::min (inScratch.size() / 2, capacity - inputHave));
                std::copy (inScratch.begin(), inScratch.begin() + 2 * got, inputFifo.begin() + (std::ptrdiff_t) (2 * inputHave));
                inputHave += (size_t) std::max (0, got);
            }
            const auto most = (size_t) (frames + 2048);
            if (inputHave > most)
            {
                const auto drop = inputHave - most;
                std::memmove (inputFifo.data(), inputFifo.data() + 2 * drop, most * 2 * sizeof (float));
                inputHave = most;
            }
        }
        const auto beatFrames = 60.0 / std::clamp (tempo.load(), 20.0, 999.0) * rate.load();

        int done = 0;
        while (done < frames)
        {
            const auto at = pos.load();
            const bool play = playing.load();
            auto n = std::min (block, frames - done);

            // A count-in: the clicks alone, the instruments live, the transport still stopped.
            if (const auto left0 = countLeft.load(); left0 > 0 && ! play)
            {
                n = (int) std::min<Frame> (n, left0);
                takeInput (n);
                renderBlock (outL.data(), outR.data(), n, at, false, true, done);
                recordBlock (-left0, at, n, false);
                click (outL.data(), outR.data(), n, countTotal - left0, beatFrames);
                for (int k = 0; k < n; ++k)
                {
                    left[done + k] += outL[(size_t) k];
                    right[done + k] += outR[(size_t) k];
                }
                liveTaken.clear();
                liveClock += n;
                done += n;
                if (countLeft.fetch_sub (n) - n <= 0)
                {
                    countLeft = 0;
                    stopPending = false;
                    if (! playing.exchange (true))
                        resetClock();
                }
                continue;
            }
            countLeft = 0;   // stopped meanwhile, or started without it

            const auto ls = loopStart.load(), le = loopEnd.load();
            const bool looping = le > ls;
            if (play && looping && at < le)
                n = (int) std::min<Frame> (n, le - at);
            // Launches start on their frame: the block ends where the next one is due. So do
            // takes, and they end on theirs.
            takeLaunches (play);
            if (play)
                for (int i = 0; i < maxTracks; ++i)
                {
                    const auto& t = lanes[(size_t) i];
                    const auto now = clock.load();
                    if (t.queuedScene != -1)
                        n = (int) std::min<Frame> (n, std::max<Frame> (1, t.queuedAt - now));
                    if (t.take.timeline)
                        continue;
                    if (t.take.state == RecordState::waiting && t.take.startAt > now)
                        n = (int) std::min<Frame> (n, t.take.startAt - now);
                    if (t.take.state == RecordState::recording && t.take.endAt > now)
                        n = (int) std::min<Frame> (n, t.take.endAt - now);
                }
            takeInput (n);
            const auto clockAt = clock.load();
            renderBlock (outL.data(), outR.data(), n, at, play, true, done);
            recordBlock (clockAt, at, n, play);
            if (play)
                clock += n;
            endPasses (n);
            // The metronome on the launch clock while the launcher plays or records, else on the timeline.
            if (play && metronome.load())
            {
                bool launcher = launcherBusy();
                for (const auto& t : lanes)
                    launcher = launcher || (t.take.state != RecordState::none && ! t.take.timeline);
                click (outL.data(), outR.data(), n, launcher ? clockAt : at, beatFrames);
            }
            for (int k = 0; k < n; ++k)
            {
                left[done + k] += outL[(size_t) k];
                right[done + k] += outR[(size_t) k];
            }
            liveTaken.clear();
            liveClock += n;
            done += n;
            if (play)
            {
                auto next = at + n;
                if (looping && next >= le)
                    next = ls;   // the next block sees the jump
                else if (! looping && next >= lengthLocked() && ! launcherBusy() && ! isRecording())   // a recording keeps time past the end
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
        std::fill (busL.begin(), busL.begin() + frames, 0.0f);
        std::fill (busR.begin(), busR.begin() + frames, 0.0f);
        const bool spatialMix = spatialOn.load() && only < 0 && spatialDecoder != nullptr;
        if (spatialMix)
        {
            for (auto& c : scene)
                std::fill (c.begin(), c.begin() + frames, 0.0f);
            std::fill (lfeBus.begin(), lfeBus.begin() + frames, 0.0f);
        }
        for (int r = 0; r < numReturns; ++r)
        {
            std::fill (returnL[(size_t) r].begin(), returnL[(size_t) r].begin() + frames, 0.0f);
            std::fill (returnR[(size_t) r].begin(), returnR[(size_t) r].begin() + frames, 0.0f);
        }
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
            // The track's delay: its clips play that much later (or earlier) than the others.
            const auto delay = delayFrames (i);
            const bool delayChanged = delay != t.delay;
            t.delay = delay;
            const auto base = (launched ? clock.load() : at) - delay;
            const bool trackJumped = (launched ? false : jumped) || t.launchChanged || delayChanged;
            t.launchChanged = false;

            // MIDI from the clips: let go of what they held when the transport stops, jumps or loses channels.
            const auto mode = (TrackMode) s.mode.load();
            const bool playMidi = play && mode != TrackMode::audio && std::any_of (clips.begin(), clips.end(), [] (const Clip& c) { return c.isMidi(); });
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
            // A take of the instrument as audio gets it as it is made, before the strip.
            if (withLive && t.take.request.fromInstrument && t.take.state != RecordState::none)
            {
                std::copy (instL.begin(), instL.begin() + frames, sourceL[(size_t) i].begin());
                std::copy (instR.begin(), instR.begin() + frames, sourceR[(size_t) i].begin());
            }

            // The audio clips: stereo ones join the instrument (both are balanced), mono ones
            // go apart to be panned with equal power. A short fade at each edge keeps a
            // trimmed clip from clicking. An instrument-only track plays none.
            std::fill (monoBuf.begin(), monoBuf.begin() + frames, 0.0f);
            std::fill (audL.begin(), audL.begin() + frames, 0.0f);
            std::fill (audR.begin(), audR.begin() + frames, 0.0f);
            bool hasMono = false, hasBeds = false;
            // In Spatial mode a surround file's front L and R go through the strip and the rest of
            // its speakers straight into the scene (placeBeds); an ambisonic file goes in whole.
            const bool inScene = spatialMix && ! s.toBus.load();
            if (play && mode != TrackMode::instrument)
                for (const auto& c : clips)
                {
                    if (c.audio == nullptr || c.muted || c.start >= base + frames || c.end() <= base)
                        continue;
                    const auto from = std::max (base, c.start), to = std::min (base + frames, c.end());
                    const auto fade = std::max<Frame> (1, std::min<Frame> (96, c.length / 4));
                    const auto& a = *c.audio;
                    const bool bed = inScene && a.surround();
                    hasBeds = hasBeds || bed;
                    if (bed && a.ambisonicOrder > 0)
                        continue;
                    const auto& fromL = bed ? a.channels[0] : a.left;
                    const auto& fromR = bed ? a.channels[1] : a.right;
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
                            audL[k] += fromL[src] * g;
                            audR[k] += fromR[src] * g;
                        }
                    }
                    hasMono = hasMono || a.mono;
                }
            // The audio input heard through the track: always, never, or while it is armed to record audio.
            if (withLive && input.load() != nullptr)
            {
                const auto monitor = s.monitor.load();
                if (monitor == Strip::monitorOn || (monitor == Strip::monitorAuto && i == armedTrack && recordsAudio (i)))
                {
                    const auto channels = s.inputChannels.load();
                    for (int k = 0; k < frames; ++k)
                    {
                        audL[(size_t) k] += channels == 2 ? inR[(size_t) k] : inL[(size_t) k];
                        audR[(size_t) k] += channels == 1 ? inL[(size_t) k] : inR[(size_t) k];
                    }
                }
            }
            layers (i, t, s, mode, hasMono, frames, jumped);
            if (routed != nullptr)
                routed->trackInput (i, blockOffset, instL.data(), instR.data(), frames);   // through the strip like the instrument

            // Trim, polarity, pan, fader, mute and solo, as one gain a side for each source.
            // A bounce takes the track before its fader and pan (trim and polarity included),
            // whatever mute and solo say; a mono file is then on both sides at full level.
            const bool bouncing = only >= 0;
            const bool audible = bouncing || (! s.mute.load() && (! anySolo || s.solo.load()));
            const auto trim = dbToGain (s.trimDb.load());
            const auto fader = audible ? (bouncing ? 1.0f : dbToGain (s.faderDb.load())) : 0.0f;
            const auto level = trim * fader;
            // In Spatial mode the 3D panner places it instead of pan and width.
            const bool placing = inScene;
            const auto pan = bouncing || placing ? 0.0f : std::clamp (s.pan.load(), -1.0f, 1.0f);
            const auto signL = s.invertLeft.load() ? -1.0f : 1.0f, signR = s.invertRight.load() ? -1.0f : 1.0f;
            const auto balanceL = std::min (1.0f, 1.0f - pan), balanceR = std::min (1.0f, 1.0f + pan);
            const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
            const auto equalL = bouncing ? 1.0f : std::cos (angle), equalR = bouncing ? 1.0f : std::sin (angle);
            const auto monoL = level * signL * equalL;
            const auto monoR = level * signR * equalR;
            const auto gainL = level * signL * balanceL, gainR = level * signR * balanceR;

            // What the strip does besides gains: the cuts, the inserts and the width.
            t.lowCut.update (s.lowCut.load(), s.lowCutHz.load(), s.lowCutSlope.load(), false, rate.load());
            t.highCut.update (s.highCut.load(), s.highCutHz.load(), s.highCutSlope.load(), true, rate.load());
            const auto width = t.mono || placing ? 1.0f : std::clamp (s.width.load(), 0.0f, 2.0f);
            const bool insertsOn = ! bypassInserts.load();
            const bool anyInsert = insertsOn && std::any_of (t.inserts.begin(), t.inserts.end(), [] (const Insert& x) { return ! x.bypassed; });
            // The sends (none in a bounce); a pre-fader one needs the signal before the fader.
            std::array<float, numReturns> sends {};
            bool anyPre = false;
            for (int r = 0; r < numReturns; ++r)
            {
                sends[(size_t) r] = bouncing || ! audible || bypassSends.load() ? 0.0f : dbToGain (s.sendDb[(size_t) r].load());
                anyPre = anyPre || (s.sendPre[(size_t) r].load() && (sends[(size_t) r] > 0.0f || t.sendGain[(size_t) r] > 0.0f));
            }
            const bool processing = t.lowCut.on || t.highCut.on || anyInsert || std::abs (width - 1.0f) > 1.0e-4f
                                 || std::abs (t.width - 1.0f) > 1.0e-4f || anyPre;
            // Before the inserts, trim and polarity; after them, pan and fader (equal power when
            // the track only has mono files, balance otherwise), and mute.
            const auto preL = trim * signL, preR = trim * signR;
            const auto postL = fader * (t.mono ? equalL : balanceL), postR = fader * (t.mono ? equalR : balanceR);

            // After a jump there is nothing to ramp from: the new place starts at its level.
            if (jumped)
            {
                t.monoL = monoL;
                t.monoR = monoR;
                t.gainL = gainL;
                t.gainR = gainR;
                t.preL = preL;
                t.preR = preR;
                t.postL = postL;
                t.postR = postR;
            }

            // The track's own sum, for its meter, then into the mix.
            auto* sumL = trackL.data();
            auto* sumR = trackR.data();
            std::fill (sumL, sumL + frames, 0.0f);
            std::fill (sumR, sumR + frames, 0.0f);
            if (processing)
            {
                // Mono files go on both sides, so the inserts get one stereo signal.
                addRamped (sumL, instL.data(), frames, t.preL, preL);
                addRamped (sumR, instR.data(), frames, t.preR, preR);
                if (hasMono)
                {
                    addRamped (sumL, monoBuf.data(), frames, t.preL, preL);
                    addRamped (sumR, monoBuf.data(), frames, t.preR, preR);
                }
                t.lowCut.process (sumL, sumR, frames);
                t.highCut.process (sumL, sumR, frames);
                if (insertsOn)
                    runInserts (t.inserts, sumL, sumR, frames);
                if (std::abs (width - 1.0f) > 1.0e-4f || std::abs (t.width - 1.0f) > 1.0e-4f)
                {
                    const auto step = (width - t.width) / (float) frames;
                    for (int k = 0; k < frames; ++k)
                    {
                        const auto w = t.width + step * (float) (k + 1);
                        const auto mid = 0.5f * (sumL[k] + sumR[k]), side = 0.5f * (sumL[k] - sumR[k]) * w;
                        sumL[k] = mid + side;
                        sumR[k] = mid - side;
                    }
                }
                if (anyPre)
                {
                    std::copy (sumL, sumL + frames, sendPreL.begin());
                    std::copy (sumR, sumR + frames, sendPreR.begin());
                }
                scaleRamped (sumL, frames, t.postL, postL);
                scaleRamped (sumR, frames, t.postR, postR);
            }
            else
            {
                addRamped (sumL, instL.data(), frames, t.gainL, gainL);
                addRamped (sumR, instR.data(), frames, t.gainR, gainR);
                if (hasMono)
                {
                    addRamped (sumL, monoBuf.data(), frames, t.monoL, monoL);
                    addRamped (sumR, monoBuf.data(), frames, t.monoR, monoR);
                }
            }
            // Both ways keep their gains, so turning the processing on or off ramps from where it was.
            t.monoL = monoL;
            t.monoR = monoR;
            t.gainL = gainL;
            t.gainR = gainR;
            t.preL = preL;
            t.preR = preR;
            t.postL = postL;
            t.postR = postR;
            t.width = width;

            for (int r = 0; r < numReturns; ++r)
            {
                auto& was = t.sendGain[(size_t) r];
                const auto g = sends[(size_t) r];
                if (jumped)
                    was = g;
                if (g > 0.0f || was > 0.0f)
                {
                    const bool pre = s.sendPre[(size_t) r].load() && processing;
                    addRamped (returnL[(size_t) r].data(), pre ? sendPreL.data() : sumL, frames, was, g);
                    addRamped (returnR[(size_t) r].data(), pre ? sendPreR.data() : sumR, frames, was, g);
                }
                was = g;
            }

            // Its sound as a key for other tracks' ducks (theirs before it in the order hear the last block's).
            for (int k = 0; k < frames; ++k)
                keyLevels[(size_t) i][(size_t) k] = std::max (std::abs (sumL[k]), std::abs (sumR[k]));
            if (withLive)
                meter (i, sumL, sumR, frames);
            // A bounce takes the track alone, never through the Pre-Master.
            const bool toBus = s.toBus.load() && only < 0;
            if (spatialMix && ! toBus)
            {
                place (sumL, sumR, frames, s.placed, trackPlaces[(size_t) i], jumped);
                // The rest of its surround and ambisonic files, at its trim and fader.
                if (hasBeds || t.bedLevel > 0.0f)
                    placeBeds (t, clips, base, frames, s.placed.place(), hasBeds ? level : 0.0f, jumped);
                // Its LFE send: both sides after the fader, to the subwoofers.
                const auto lfe = audible ? dbToGain (s.lfeDb.load()) : 0.0f;
                if (jumped)
                    t.lfeGain = lfe;
                if (lfe > 0.0f || t.lfeGain > 0.0f)
                {
                    addRamped (lfeBus.data(), sumL, frames, t.lfeGain, lfe);
                    addRamped (lfeBus.data(), sumR, frames, t.lfeGain, lfe);
                }
                t.lfeGain = lfe;
                continue;
            }
            if (routed != nullptr && ! toBus)
            {
                routed->trackOutput (i, blockOffset, sumL, sumR, frames);
                continue;
            }
            auto& toL = toBus ? busL : mixL;
            auto& toR = toBus ? busR : mixR;
            for (int k = 0; k < frames; ++k)
            {
                toL[(size_t) k] += sumL[k];
                toR[(size_t) k] += sumR[k];
            }
        }
        // The Pre-Master, before the returns: its sends feed them too. With the patchbay it goes
        // with the returns into the Mix Bus (the tracks sent to it skip their own patch).
        if (only < 0)
            renderBus (frames, jumped, withLive, spatialMix);
        // The returns: each through its effect, pan, fader and mute, then with the tracks.
        if (only < 0)
            for (int r = 0; r < numReturns; ++r)
            {
                auto* rl = returnL[(size_t) r].data();
                auto* rr = returnR[(size_t) r].data();
                if (auto& fx = returnEffects[(size_t) r])
                    fx->process (rl, rr, frames);
                const auto& rs = returnStrips[(size_t) r];
                const auto level = rs.mute.load() ? 0.0f : dbToGain (rs.faderDb.load());
                const auto pan = spatialMix ? 0.0f : std::clamp (rs.pan.load(), -1.0f, 1.0f);
                const float gains[2] = { level * std::min (1.0f, 1.0f - pan), level * std::min (1.0f, 1.0f + pan) };
                auto& was = returnGain[(size_t) r];
                if (jumped)
                    was = { gains[0], gains[1] };
                scaleRamped (rl, frames, was[0], gains[0]);
                scaleRamped (rr, frames, was[1], gains[1]);
                was = { gains[0], gains[1] };
                if (withLive)
                    meter (returnIndex + r, rl, rr, frames);
                if (spatialMix)
                {
                    place (rl, rr, frames, rs.placed, returnPlaces[(size_t) r], jumped);
                    continue;
                }
                for (int k = 0; k < frames; ++k)
                {
                    mixL[(size_t) k] += rl[k];
                    mixR[(size_t) k] += rr[k];
                }
            }
        // The scene decoded: the headphone mix joins the stereo out, the speakers get their feeds.
        if (spatialMix)
        {
            // A copy of the scene for an AmbiX mix down, before the decoder turns it.
            if (only < 0 && ! withLive && spatialRender.load() == SpatialRender::ambix)
                for (int c = 0; c < spatial::channels; ++c)
                    std::copy (scene[(size_t) c].begin(), scene[(size_t) c].begin() + frames, feeds[(size_t) c].begin());
            else
            {
                spatialDecoder->process (sceneAt.data(), frames, mixL.data(), mixR.data(), feedAt.data());
                // The subwoofers: the LFE sends and the files' LFE channels, below 120 Hz.
                const auto& subs = spatialDecoder->setup().lfe;
                if (! subs.empty())
                {
                    lfeFilter.update (true, 120.0f, 24, true, rate.load());
                    std::fill (lfeScratch.begin(), lfeScratch.begin() + frames, 0.0f);
                    lfeFilter.process (lfeBus.data(), lfeScratch.data(), frames);
                    for (const auto l : subs)
                        if (l >= 0 && l < spatialDecoder->speakerCount())
                            for (int k = 0; k < frames; ++k)
                                feeds[(size_t) l][(size_t) k] += lfeBus[(size_t) k];
                }
            }
            if (withLive)
                feedSpeakers (frames);
        }
        if (routed != nullptr)
        {
            // The patch has the tracks, and does the master; the returns go to its Mix Bus.
            std::copy (mixL.begin(), mixL.begin() + frames, left);
            std::copy (mixR.begin(), mixR.begin() + frames, right);
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

    void Engine::runInserts (Inserts& list, float* left, float* right, int frames)
    {
        for (auto& x : list)
        {
            if (x.bypassed)
                continue;
            // A slot's Mix below 100 % keeps some of what went in: parallel processing.
            const auto mix = std::clamp (x.mix, 0.0f, 1.0f);
            if (mix < 0.999f)
            {
                std::copy (left, left + frames, dryL.begin());
                std::copy (right, right + frames, dryR.begin());
            }
            x.effect->process (left, right, frames);
            if (mix < 0.999f)
                for (int k = 0; k < frames; ++k)
                {
                    left[k] = dryL[(size_t) k] + (left[k] - dryL[(size_t) k]) * mix;
                    right[k] = dryR[(size_t) k] + (right[k] - dryR[(size_t) k]) * mix;
                }
        }
    }

    void Engine::renderBus (int frames, bool jumped, bool withLive, bool spatialMix)
    {
        auto* bl = busL.data();
        auto* br = busR.data();
        const auto trim = dbToGain (std::clamp (bus.trimDb.load(), -24.0f, 24.0f));
        if (jumped)
            busTrim = trim;
        scaleRamped (bl, frames, busTrim, trim);
        scaleRamped (br, frames, busTrim, trim);
        busTrim = trim;
        if (! bypassInserts.load())
            runInserts (busInserts, bl, br, frames);
        const auto width = spatialMix ? 1.0f : std::clamp (bus.width.load(), 0.0f, 2.0f);
        if (jumped)
            busWidth = width;
        if (std::abs (width - 1.0f) > 1.0e-4f || std::abs (busWidth - 1.0f) > 1.0e-4f)
        {
            const auto step = (width - busWidth) / (float) frames;
            for (int k = 0; k < frames; ++k)
            {
                const auto w = busWidth + step * (float) (k + 1);
                const auto mid = 0.5f * (bl[k] + br[k]), side = 0.5f * (bl[k] - br[k]) * w;
                bl[k] = mid + side;
                br[k] = mid - side;
            }
        }
        busWidth = width;
        // A pre-fader send takes it here, after the inserts and width.
        const bool audible = ! bus.mute.load();
        std::array<float, numReturns> sends {};
        for (int r = 0; r < numReturns; ++r)
            sends[(size_t) r] = ! audible || bypassSends.load() ? 0.0f : dbToGain (bus.sendDb[(size_t) r].load());
        const bool anyPre = std::any_of (bus.sendPre.begin(), bus.sendPre.end(), [] (const std::atomic<bool>& p) { return p.load(); });
        if (anyPre)
        {
            std::copy (bl, bl + frames, sendPreL.begin());
            std::copy (br, br + frames, sendPreR.begin());
        }
        const auto fader = audible ? dbToGain (bus.faderDb.load()) : 0.0f;
        const auto pan = spatialMix ? 0.0f : std::clamp (bus.pan.load(), -1.0f, 1.0f);
        const auto gainL = fader * std::min (1.0f, 1.0f - pan), gainR = fader * std::min (1.0f, 1.0f + pan);
        if (jumped)
        {
            busGainL = gainL;
            busGainR = gainR;
        }
        scaleRamped (bl, frames, busGainL, gainL);
        scaleRamped (br, frames, busGainR, gainR);
        busGainL = gainL;
        busGainR = gainR;
        for (int r = 0; r < numReturns; ++r)
        {
            auto& was = busSendGain[(size_t) r];
            const auto g = sends[(size_t) r];
            if (jumped)
                was = g;
            if (g > 0.0f || was > 0.0f)
            {
                const bool pre = bus.sendPre[(size_t) r].load();
                addRamped (returnL[(size_t) r].data(), pre ? sendPreL.data() : bl, frames, was, g);
                addRamped (returnR[(size_t) r].data(), pre ? sendPreR.data() : br, frames, was, g);
            }
            was = g;
        }
        if (withLive)
            meter (busIndex, bl, br, frames);
        if (spatialMix)
        {
            place (bl, br, frames, bus.placed, busPlace, jumped);
            return;
        }
        for (int k = 0; k < frames; ++k)
        {
            mixL[(size_t) k] += bl[k];
            mixR[(size_t) k] += br[k];
        }
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
            for (auto& x : t.inserts)
                x.effect->reset();
            t.lowCut.hz = t.highCut.hz = 0.0f;
            t.lowCut.on = t.highCut.on = false;   // their filters start from silence
            t.held = {};
            t.relocate = true;
        }
        for (auto& fx : returnEffects)
            if (fx != nullptr)
                fx->reset();
        for (auto& x : busInserts)
            x.effect->reset();
        // A spatial mix down is the headphone mix, from silence and with the decoder's delay taken out.
        // Or, as asked, the speakers' feeds or the AmbiX scene, at the master's level.
        const bool spatialMix = spatialOn.load() && only < 0 && spatialDecoder != nullptr;
        const auto render = spatialMix ? spatialRender.load() : SpatialRender::headphones;
        const int outChannels = render == SpatialRender::speakers ? spatialDecoder->speakerCount()
                              : render == SpatialRender::ambix ? spatial::channels : 2;
        if (outChannels == 0)
        {
            error = "There are no speakers to mix down to: pick a speaker array in Spatial.";
            only = -1;
            return nullptr;
        }
        if (spatialMix)
            spatialDecoder->reset();
        lfeFilter = {};
        int skip = render == SpatialRender::headphones && spatialMix ? spatialDecoder->latency() : 0;
        const auto masterLevel = dbToGain (masterDb.load());
        for (auto& k : keyLevels)
            std::fill (k.begin(), k.end(), 0.0f);
        stopPending = false;
        expected = -1;

        // After the end, it goes on while the instruments still ring (a quarter second of silence ends it).
        const auto maxTail = (Frame) std::llround (std::max (0.0, tailSeconds) * r);
        const auto quietNeeded = (Frame) std::llround (0.25 * r);
        AudioData::Builder out (outChannels, r);
        std::vector<float> left (block), right (block);
        std::vector<const float*> ptrs ((size_t) outChannels);
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
            const auto drop = std::min (skip, n);
            skip -= drop;
            float peak = 0.0f;
            if (render == SpatialRender::headphones)
            {
                ptrs[0] = left.data() + drop;
                ptrs[1] = right.data() + drop;
                for (int k = 0; k < n; ++k)
                    peak = std::max ({ peak, std::abs (left[(size_t) k]), std::abs (right[(size_t) k]) });
            }
            else
                for (int c = 0; c < outChannels; ++c)
                {
                    auto* feed = feeds[(size_t) c].data();
                    for (int k = 0; k < n; ++k)
                    {
                        feed[k] *= masterLevel;
                        peak = std::max (peak, std::abs (feed[k]));
                    }
                    ptrs[(size_t) c] = feed + drop;
                }
            if (n > drop)
                out.append (ptrs.data(), n - drop);
            at += n;
            if (at > end)
            {
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
            for (auto& x : t.inserts)
                x.effect->reset();
            t.lowCut.hz = t.highCut.hz = 0.0f;
            t.lowCut.on = t.highCut.on = false;   // their filters start from silence
            t.held = {};
            t.relocate = true;
        }
        if (spatialMix)
            spatialDecoder->reset();
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
