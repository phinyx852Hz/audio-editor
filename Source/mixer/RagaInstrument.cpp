// SPDX-License-Identifier: AGPL-3.0-only
#include "RagaInstrument.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixer
{
    namespace
    {
        double keyHz (int key) { return 440.0 * std::pow (2.0, (key - 69) / 12.0); }
    }

    void RagaInstrument::prepare (double sampleRate, int)
    {
        rate = sampleRate;
        sitar.prepare (rate);
        std::lock_guard<std::mutex> l (lock);
        changed = true;
    }

    void RagaInstrument::setSettings (const RagaSettings& s)
    {
        std::lock_guard<std::mutex> l (lock);
        current = s;
        current.sa = std::clamp (current.sa, 0, 11);
        changed = true;
    }

    RagaSettings RagaInstrument::settings() const
    {
        std::lock_guard<std::mutex> l (lock);
        return current;
    }

    void RagaInstrument::setRaga (int index, int root, bool equalTempered)
    {
        std::lock_guard<std::mutex> l (lock);
        if (index == forced && root == forcedRoot && equalTempered == forcedEqual)
            return;
        forced = index < (int) phi::builtInRagas().size() ? index : -1;
        forcedRoot = std::clamp (root, 0, 11);
        forcedEqual = equalTempered;
        changed = true;
    }

    std::string RagaInstrument::playing() const
    {
        std::lock_guard<std::mutex> l (lock);
        const auto& all = phi::builtInRagas();
        if (forced >= 0)
            return all[(size_t) forced].name + (forcedEqual ? " (equal tempered)" : "");
        for (const auto& r : all)
            if (r.plain == current.raga)
                return r.name;
        return {};
    }

    void RagaInstrument::applyPending()
    {
        std::unique_lock<std::mutex> l (lock, std::try_to_lock);
        if (! l.owns_lock() || ! changed)
            return;
        changed = false;
        const auto& all = phi::builtInRagas();
        int index = forced, sa = forcedRoot;
        if (index < 0)
        {
            sa = current.sa;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].plain == current.raga)
                    index = (int) i;
        }
        if (index < 0 && ! all.empty())
            index = 0;
        if (index < 0)
            return;
        raga = all[(size_t) index];
        if (forced >= 0 && forcedEqual)
            for (auto& c : raga.cents)
                c = 100.0 * std::round (c / 100.0);
        const auto saKey = 60 + sa;
        const auto saHz = keyHz (saKey);
        player.setRaga (&raga, saKey, saHz);
        player.setOrnaments (current.ornaments);
        sitar.setJawari (current.jawari);
        sitar.setTarabLevel (current.tarab);
        sitar.setChikari (saHz);
        // The tarab: the raga's svaras from Sa up, thirteen of them.
        std::vector<double> tarab;
        for (int octave = 0; tarab.size() < (size_t) phi::Sitar::maxTarab && octave < 4; ++octave)
            for (int s = 0; s < 12 && tarab.size() < (size_t) phi::Sitar::maxTarab; ++s)
                if (raga.uses[(size_t) s])
                    tarab.push_back (saHz * std::pow (2.0, octave + raga.cents[(size_t) s] / 1200.0));
        sitar.setTarab (tarab);
    }

    void RagaInstrument::handle (const NoteEvent& e)
    {
        applyPending();
        const auto type = e.status & 0xF0;
        const bool on = type == 0x90 && e.data2 > 0, off = type == 0x80 || (type == 0x90 && e.data2 == 0);
        if (on)
        {
            // The lowest keys, two octaves and more below Sa, strike the chikārī.
            if (e.data1 < player.voiceSaKey() - 24)
            {
                sitar.strikeChikari (e.data2 / 127.0);
                return;
            }
            Held h;
            h.key = e.data1;
            h.voice = player.noteOn (e.data1);
            h.string = sitar.pluck (h.voice.hz, e.data2 / 127.0);
            for (auto& other : held)
                if (other.string == h.string)
                    other = {};
            held[(size_t) h.string] = h;
        }
        else if (off)
        {
            for (auto& h : held)
                if (h.key == e.data1 && h.string >= 0)
                {
                    sitar.release (h.string);
                    h = {};
                }
        }
        else if (type == 0xE0)
            bend = ((e.data2 << 7 | e.data1) - 8192) / 8192.0 * 2.0;   // ±2 semitones
        else if (type == 0xB0 && (e.data1 == 120 || e.data1 == 123))
            releaseAll();
    }

    void RagaInstrument::render (float* left, float* right, int frames)
    {
        applyPending();
        constexpr int chunk = 32;   // ornaments move the pitch every 32 samples (under a millisecond)
        const auto bendRatio = std::pow (2.0, bend / 12.0);
        for (int done = 0; done < frames; done += chunk)
        {
            const auto n = std::min (chunk, frames - done);
            for (auto& h : held)
                if (h.string >= 0)
                {
                    sitar.setHz (h.string, player.hzAt (h.voice, h.seconds) * bendRatio);
                    h.seconds += n / rate;
                }
            sitar.process (left + done, right + done, n);
        }
    }

    void RagaInstrument::releaseAll()
    {
        for (auto& h : held)
            if (h.string >= 0)
            {
                sitar.release (h.string);
                h = {};
            }
        player.reset();
    }

    void RagaInstrument::reset()
    {
        held = {};
        sitar.reset();
        player.reset();
        bend = 0.0;
    }

    std::string RagaInstrument::state() const
    {
        const auto s = settings();
        std::ostringstream o;
        o << "raga=" << s.raga << ";sa=" << s.sa << ";ornaments=" << s.ornaments << ";jawari=" << s.jawari << ";tarab=" << s.tarab;
        return o.str();
    }

    void RagaInstrument::setState (const std::string& text)
    {
        RagaSettings s;
        std::istringstream in (text);
        std::string part;
        while (std::getline (in, part, ';'))
        {
            const auto eq = part.find ('=');
            if (eq == std::string::npos)
                continue;
            const auto key = part.substr (0, eq), value = part.substr (eq + 1);
            try
            {
                if (key == "raga")
                    s.raga = value;
                else if (key == "sa")
                    s.sa = std::stoi (value);
                else if (key == "ornaments")
                    s.ornaments = std::stod (value);
                else if (key == "jawari")
                    s.jawari = std::stod (value);
                else if (key == "tarab")
                    s.tarab = std::stod (value);
            }
            catch (...)
            {
            }
        }
        setSettings (s);
    }
}
