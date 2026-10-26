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
        tanpura.prepare (rate);
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
        meendOn = current.meend;
        meendTime = current.meendSeconds;
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
        // The tānpūrā's first string: Pa, or the raga's ma, or its ni, when it has no Pa.
        const int first = raga.uses[7] ? 7 : raga.uses[5] ? 5 : raga.uses[11] ? 11 : raga.uses[10] ? 10 : 7;
        tanpura.tune (saHz, raga.uses[(size_t) first] ? raga.cents[(size_t) first] : phi::justCents (first));
        tanpura.setLevel (current.tanpuraLevel);
        tanpura.setCycle (current.tanpuraSeconds);
        tanpura.setOn (current.tanpura);
    }

    void RagaInstrument::handle (const NoteEvent& e)
    {
        applyPending();
        // MPE: each note's bend and pressure on its own channel; plain MIDI is the same, one channel.
        const auto m = mpe.handle (e.status, e.data1, e.data2);
        if (m.kind == phi::Mpe::Kind::noteOn)
        {
            const auto velocity = std::max (1.0 / 127.0, m.velocity);
            // The lowest keys, two octaves and more below Sa, strike the chikārī.
            if (m.key < player.voiceSaKey() - 24)
            {
                sitar.strikeChikari (velocity);
                return;
            }
            // Legato: the string still held is pulled to the new svara (mīṇḍ), not plucked again.
            Held* last = nullptr;
            for (auto& h : held)
                if (h.string >= 0 && (last == nullptr || startedAt[(size_t) h.string] > startedAt[(size_t) last->string]))
                    last = &h;
            if (meendOn && last != nullptr)
            {
                // From where the string is now, mid-ornament or not.
                auto from = last->voice;
                from.cents = 1200.0 * std::log2 (player.hzAt (last->voice, last->seconds) / player.voiceSaHz());
                last->voice = player.glide (from, m.key, meendTime);
                last->key = m.key;
                last->channel = m.channel;
                last->seconds = 0.0;
                startedAt[(size_t) last->string] = ++clock;
                return;
            }
            Held h;
            h.key = m.key;
            h.channel = m.channel;
            h.voice = player.noteOn (m.key);
            player.ornament (h.voice);
            h.string = sitar.pluck (player.hzAt (h.voice, 0.0) * std::pow (2.0, mpe.expression (m.channel, m.key).bendSemitones / 12.0), velocity);
            for (auto& other : held)
                if (other.string == h.string)
                    other = {};
            held[(size_t) h.string] = h;
            startedAt[(size_t) h.string] = ++clock;
        }
        else if (m.kind == phi::Mpe::Kind::noteOff)
        {
            for (auto& h : held)
                if (h.key == m.key && h.channel == m.channel && h.string >= 0)
                {
                    sitar.release (h.string);
                    h = {};
                }
        }
        else if (m.kind == phi::Mpe::Kind::allNotesOff)
            releaseAll();
    }

    void RagaInstrument::render (float* left, float* right, int frames)
    {
        applyPending();
        constexpr int chunk = 32;   // ornaments move the pitch every 32 samples (under a millisecond)
        for (int done = 0; done < frames; done += chunk)
        {
            const auto n = std::min (chunk, frames - done);
            for (auto& h : held)
                if (h.string >= 0)
                {
                    const auto bent = std::pow (2.0, mpe.expression (h.channel, h.key).bendSemitones / 12.0);
                    sitar.setHz (h.string, player.hzAt (h.voice, h.seconds) * bent);
                    h.seconds += n / rate;
                }
            sitar.process (left + done, right + done, n);
            tanpura.process (left + done, right + done, n);
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
        tanpura.reset();
        player.reset();
        mpe.reset();
    }

    std::string RagaInstrument::state() const
    {
        const auto s = settings();
        std::ostringstream o;
        o << "raga=" << s.raga << ";sa=" << s.sa << ";ornaments=" << s.ornaments << ";jawari=" << s.jawari << ";tarab=" << s.tarab
          << ";meend=" << (s.meend ? 1 : 0) << ";meendSeconds=" << s.meendSeconds << ";tanpura=" << (s.tanpura ? 1 : 0)
          << ";tanpuraLevel=" << s.tanpuraLevel << ";tanpuraSeconds=" << s.tanpuraSeconds;
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
                else if (key == "meend")
                    s.meend = value != "0";
                else if (key == "meendSeconds")
                    s.meendSeconds = std::stod (value);
                else if (key == "tanpura")
                    s.tanpura = value != "0";
                else if (key == "tanpuraLevel")
                    s.tanpuraLevel = std::stod (value);
                else if (key == "tanpuraSeconds")
                    s.tanpuraSeconds = std::stod (value);
            }
            catch (...)
            {
            }
        }
        setSettings (s);
    }
}
