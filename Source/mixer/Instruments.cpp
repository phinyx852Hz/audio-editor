// SPDX-License-Identifier: AGPL-3.0-only
#include "Instruments.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixer
{
    void NoteInstrument::process (const NoteEvent* events, int numEvents, float* left, float* right, int frames)
    {
        int done = 0;
        for (int i = 0; i < numEvents; ++i)
        {
            const auto at = std::clamp (events[i].offset, done, frames);
            if (at > done)
                render (left + done, right + done, at - done);
            done = at;
            handle (events[i]);
        }
        if (frames > done)
            render (left + done, right + done, frames - done);
    }

    //==========================================================================
    namespace
    {
        constexpr double twoPi = 6.283185307179586;

        double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

        // PolyBLEP: takes the corner off a saw or square's jump so it aliases far less.
        double blep (double t, double dt)
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }

        // General MIDI drum notes to the few sounds the Synth makes.
        enum Drum { none, kick, snare, clap, closedHat, openHat, tom, crash, ride, click };
        Drum drumFor (int note)
        {
            switch (note)
            {
                case 35: case 36: return kick;
                case 38: case 40: return snare;
                case 37: case 39: return clap;
                case 42: case 44: return closedHat;
                case 46: return openHat;
                case 41: case 43: case 45: case 47: case 48: case 50: return tom;
                case 49: case 52: case 55: case 57: return crash;
                case 51: case 53: case 59: return ride;
                default: return click;
            }
        }
    }

    void Synth::prepare (double sampleRate, int)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void Synth::setSettings (const SynthSettings& s)
    {
        const std::lock_guard<std::mutex> l (lock);
        current = s;
        current.voices = std::clamp (current.voices, 1, maxVoices);
    }

    SynthSettings Synth::settings() const
    {
        const std::lock_guard<std::mutex> l (lock);
        return current;
    }

    std::string Synth::state() const
    {
        const auto s = settings();
        std::ostringstream out;
        out << s.wave << ' ' << s.attack << ' ' << s.decay << ' ' << s.sustain << ' ' << s.release << ' '
            << s.cutoff << ' ' << s.gainDb << ' ' << (s.drums ? 1 : 0) << ' ' << s.voices;
        return out.str();
    }

    void Synth::setState (const std::string& text)
    {
        SynthSettings s;
        std::istringstream in (text);
        int drums = 1;
        in >> s.wave >> s.attack >> s.decay >> s.sustain >> s.release >> s.cutoff >> s.gainDb >> drums >> s.voices;
        if (in.fail())
            return;
        s.wave = std::clamp (s.wave, 0, 3);
        s.drums = drums != 0;
        setSettings (s);
    }

    void Synth::releaseAll()
    {
        for (auto& c : channel)
            c.pedal = false;
        for (auto& v : voice)
            if (v.active)
            {
                v.held = v.sustained = false;
                v.stage = 3;
            }
    }

    void Synth::reset()
    {
        voice = {};
        channel = {};
    }

    float Synth::noise()
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) ((int32_t) seed) * (1.0f / 2147483648.0f);
    }

    Synth::Voice& Synth::freeVoice()
    {
        const auto count = std::clamp (live.voices, 1, maxVoices);
        Voice* best = nullptr;
        for (int i = 0; i < count; ++i)
            if (! voice[(size_t) i].active)
                return voice[(size_t) i];
        // All busy: the oldest released one, else the oldest.
        for (int i = 0; i < count; ++i)
        {
            auto& v = voice[(size_t) i];
            if (best == nullptr || (v.stage == 3 && best->stage != 3) || ((v.stage == 3) == (best->stage == 3) && v.age < best->age))
                best = &v;
        }
        return *best;
    }

    void Synth::noteOn (int ch, int note, float velocity)
    {
        const bool drums = live.drums && ch == 9;
        auto& v = freeVoice();
        v = {};
        v.active = v.held = true;
        v.channel = ch;
        v.note = note;
        v.velocity = velocity;
        v.age = ++clock;
        if (! drums)
        {
            v.frequency = noteHz (note);
            return;
        }

        v.drum = drumFor (note);
        // An open hat is cut by the closed one.
        if (v.drum == closedHat)
            for (auto& o : voice)
                if (o.active && &o != &v && o.drum == openHat)
                    o.drumDecay = std::min (o.drumDecay, 0.01);
        switch (v.drum)
        {
            case kick:      v.frequency = 50.0;  v.sweep = 110.0; v.drumDecay = 0.32; break;
            case snare:     v.frequency = 185.0; v.drumDecay = 0.17; break;
            case clap:      v.drumDecay = 0.12; break;
            case closedHat: v.drumDecay = 0.045; break;
            case openHat:   v.drumDecay = 0.32; break;
            case tom:       v.frequency = noteHz (note - 12); v.sweep = v.frequency * 0.6; v.drumDecay = 0.35; break;
            case crash:     v.drumDecay = 1.3; break;
            case ride:      v.frequency = 3200.0; v.drumDecay = 0.7; break;
            default:        v.drumDecay = 0.03; break;
        }
        v.level = 1.0;
    }

    void Synth::noteOff (int ch, int note)
    {
        for (auto& v : voice)
            if (v.active && v.held && v.channel == ch && v.note == note)
            {
                v.held = false;
                if (v.drum != 0)
                    continue;   // drums ring out
                if (channel[(size_t) ch].pedal)
                    v.sustained = true;
                else
                    v.stage = 3;
            }
    }

    void Synth::handle (const NoteEvent& e)
    {
        const auto ch = e.status & 0x0F;
        auto& c = channel[(size_t) ch];
        switch (e.status & 0xF0)
        {
            case 0x90:
                if (e.data2 > 0)
                    noteOn (ch, e.data1, (float) e.data2 / 127.0f);
                else
                    noteOff (ch, e.data1);
                break;
            case 0x80: noteOff (ch, e.data1); break;
            case 0xE0: c.bend = ((e.data2 << 7 | e.data1) - 8192) / 8192.0; break;
            case 0xB0:
                switch (e.data1)
                {
                    case 7:  c.volume = (float) e.data2 / 127.0f; break;
                    case 10: c.pan = ((float) e.data2 - 64.0f) / 63.0f; break;
                    case 11: c.expression = (float) e.data2 / 127.0f; break;
                    case 64:
                        c.pedal = e.data2 >= 64;
                        if (! c.pedal)
                            for (auto& v : voice)
                                if (v.active && v.channel == ch && v.sustained)
                                {
                                    v.sustained = false;
                                    v.stage = 3;
                                }
                        break;
                    case 120:
                        for (auto& v : voice)
                            if (v.channel == ch)
                                v.active = false;
                        break;
                    case 121: c = {}; break;
                    case 123:
                        for (auto& v : voice)
                            if (v.active && v.channel == ch && v.drum == 0)
                            {
                                v.held = v.sustained = false;
                                v.stage = 3;
                            }
                        break;
                    default: break;
                }
                break;
            default: break;
        }
    }

    void Synth::render (float* left, float* right, int frames)
    {
        {
            std::unique_lock<std::mutex> l (lock, std::try_to_lock);
            if (l.owns_lock())
                live = current;
        }
        const auto gain = (float) std::pow (10.0, live.gainDb / 20.0);
        const auto dt = 1.0 / rate;
        const auto attackStep = dt / std::max (0.001, live.attack);
        const auto decayCoef = std::exp (-6.9 * dt / std::max (0.001, live.decay));      // 60 dB over the time
        const auto releaseCoef = std::exp (-6.9 * dt / std::max (0.001, live.release));
        const auto sustain = std::clamp (live.sustain, 0.0, 1.0);

        for (auto& v : voice)
        {
            if (! v.active)
                continue;
            const auto& c = channel[(size_t) v.channel];
            const auto level = gain * c.volume * c.expression * (v.drum != 0 ? 1.0f : v.velocity * v.velocity);
            const auto panL = (float) std::cos ((c.pan + 1.0f) * 0.25 * 3.141592653589793) * 1.41421356f;
            const auto panR = (float) std::sin ((c.pan + 1.0f) * 0.25 * 3.141592653589793) * 1.41421356f;
            const auto l = level * std::min (1.0f, panL), r = level * std::min (1.0f, panR);

            if (v.drum != 0)
            {
                const auto decay = std::exp (-6.9 * dt / std::max (0.005, v.drumDecay));
                const auto vel = v.velocity;
                for (int i = 0; i < frames && v.active; ++i)
                {
                    double s = 0.0;
                    const auto n = noise();
                    switch (v.drum)
                    {
                        case kick:
                        case tom:
                        {
                            const auto f = v.frequency + v.sweep * std::exp (-v.time * (v.drum == kick ? 28.0 : 14.0));
                            v.phase += f * dt;
                            s = std::sin (twoPi * v.phase) * 1.2;
                            if (v.time < 0.002)
                                s += n * 0.3;   // the beater
                            break;
                        }
                        case snare:
                        {
                            v.phase += v.frequency * dt;
                            v.noiseLow += 0.25 * (n - v.noiseLow);
                            s = 0.55 * (n - v.noiseLow) + 0.6 * std::sin (twoPi * v.phase) * std::exp (-v.time * 30.0);
                            break;
                        }
                        case clap:
                        {
                            // Three quick bursts, then the tail.
                            const auto burst = v.time < 0.03 ? (std::fmod (v.time, 0.01) < 0.004 ? 1.0 : 0.25) : 1.0;
                            v.noiseLow += 0.35 * (n - v.noiseLow);
                            s = 0.8 * burst * (n - v.noiseLow);
                            break;
                        }
                        case ride:
                            v.phase += v.frequency * dt;
                            v.noiseLow += 0.5 * (n - v.noiseLow);
                            s = 0.25 * (n - v.noiseLow) + 0.15 * std::sin (twoPi * v.phase);
                            break;
                        default:
                            // Hats, cymbals and clicks: noise with the low end taken out.
                            v.noiseLow += 0.6 * (n - v.noiseLow);
                            s = (v.drum == crash ? 0.45 : 0.5) * (n - v.noiseLow);
                            break;
                    }
                    const auto out = (float) (s * v.level) * vel;
                    left[i] += out * l;
                    right[i] += out * r;
                    v.level *= decay;
                    v.time += dt;
                    if (v.level < 1.0e-4)
                        v.active = false;
                }
                continue;
            }

            const auto frequency = v.frequency * std::pow (2.0, c.bend * 2.0 / 12.0);
            const auto inc = std::min (0.45, frequency * dt);
            // A two-pole state-variable low-pass, opened a little more by velocity.
            const auto fc = std::clamp (live.cutoff * (0.5 + v.velocity), 30.0, rate * 0.45);
            const auto g = std::tan (3.141592653589793 * fc / rate);
            const auto k = 1.2, a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;

            for (int i = 0; i < frames; ++i)
            {
                double s = 0.0;
                switch (live.wave)
                {
                    case SynthSettings::sine:     s = std::sin (twoPi * v.phase); break;
                    case SynthSettings::triangle: s = 1.0 - 4.0 * std::abs (v.phase - 0.5); break;
                    case SynthSettings::square:
                    {
                        s = v.phase < 0.5 ? 1.0 : -1.0;
                        s += blep (v.phase, inc);
                        s -= blep (std::fmod (v.phase + 0.5, 1.0), inc);
                        s *= 0.6;
                        break;
                    }
                    default:
                        s = 2.0 * v.phase - 1.0 - blep (v.phase, inc);
                        s *= 0.7;
                        break;
                }
                v.phase += inc;
                if (v.phase >= 1.0)
                    v.phase -= 1.0;

                const auto v3 = s - v.low2;
                const auto v1 = a1 * v.band + a2 * v3;
                const auto v2 = v.low2 + a2 * v.band + a3 * v3;
                v.band = 2.0 * v1 - v.band;
                v.low2 = 2.0 * v2 - v.low2;

                switch (v.stage)
                {
                    case 0:
                        v.level += attackStep;
                        if (v.level >= 1.0)
                        {
                            v.level = 1.0;
                            v.stage = 1;
                        }
                        break;
                    case 1:
                        v.level = sustain + (v.level - sustain) * decayCoef;
                        if (v.level - sustain < 1.0e-4)
                            v.stage = 2;
                        break;
                    case 2: v.level = sustain; break;
                    default: v.level *= releaseCoef; break;
                }
                const auto out = (float) (v2 * v.level);
                left[i] += out * l;
                right[i] += out * r;
                if ((v.stage == 3 || (v.stage == 2 && sustain <= 0.0)) && v.level < 1.0e-4)
                {
                    v.active = false;
                    break;
                }
            }
        }
    }

    //==========================================================================
    void SamplerInstrument::handle (const NoteEvent& e)
    {
        switch (e.status & 0xF0)
        {
            case 0x90: sound.noteOn (e.data1, (float) e.data2 / 127.0f); break;
            case 0x80: sound.noteOff (e.data1); break;
            case 0xE0: sound.pitchBend (((e.data2 << 7 | e.data1) - 8192) / 8192.0); break;
            case 0xB0:
                if (e.data1 == 64)
                    sound.sustainPedal (e.data2 >= 64);
                else if (e.data1 == 120)
                    sound.panic();
                else if (e.data1 == 123)
                    sound.allNotesOff();
                break;
            default: break;
        }
    }

    void SlicerInstrument::handle (const NoteEvent& e)
    {
        switch (e.status & 0xF0)
        {
            case 0x90:
                if (e.data2 > 0)
                    sound.noteOn (e.data1, (float) e.data2 / 127.0f);
                else
                    sound.noteOff (e.data1);
                break;
            case 0x80: sound.noteOff (e.data1); break;
            case 0xB0:
                if (e.data1 == 120)
                    sound.panic();
                else if (e.data1 == 123)
                    releaseAll();
                break;
            default: break;
        }
    }

    void SlicerInstrument::releaseAll()
    {
        for (int pad = 0; pad < slicer::numPads; ++pad)
            sound.letGo (pad);
    }
}
