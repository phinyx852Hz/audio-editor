// SPDX-License-Identifier: AGPL-3.0-only
#include "Tunings.h"

#include "../core/PhiRaga.h"

#include <algorithm>
#include <cmath>

namespace mixer
{
    namespace
    {
        double cents (double ratio) { return phi::Tuning::ratioToCents (ratio); }

        // The white notes in Pythagorean tuning (chains of 3/2), read from the `start`th of them.
        std::vector<double> pythagorean (int start)
        {
            static const double white[7] { 0.0, cents (9.0 / 8), cents (81.0 / 64), cents (4.0 / 3),
                                           cents (3.0 / 2), cents (27.0 / 16), cents (243.0 / 128) };
            std::vector<double> out;
            for (int i = 0; i < 7; ++i)
            {
                auto c = white[(start + i) % 7] - white[start];
                out.push_back (c < 0 ? c + 1200.0 : c);
            }
            return out;
        }

        std::vector<double> semitones (std::initializer_list<int> steps)
        {
            std::vector<double> out;
            for (auto s : steps)
                out.push_back (s * 100.0);
            return out;
        }

        std::vector<TuningSystem> build()
        {
            std::vector<TuningSystem> all;
            // Ragas, from the raga sheets (PhiRagas.cpp): each in its own tuning, just intonation
            // with the Pythagorean komal svaras the raga sings low.
            for (const auto& raga : phi::builtInRagas())
                all.push_back ({ raga.name, raga.plain, "raga", raga.scaleCents(), 0 });
            // Each raga again in 12-TET, its svaras on the piano's keys.
            for (size_t i = 0, ragas = all.size(); i < ragas; ++i)
            {
                auto t = all[i];
                t.name += " (equal tempered)";
                t.plain += " (equal tempered)";
                t.family = "raga, equal tempered";
                for (auto& c : t.cents)
                    c = 100.0 * std::round (c / 100.0);
                all.push_back (std::move (t));
            }

            // Church modes, from their final. A hypo-mode spans the octave a fourth below its
            // final, so its root here is the bottom of that range (Hypodorian on A for a D final).
            for (auto [name, start] : { std::pair { "dorian", 1 }, { "phrygian", 2 }, { "lydian", 3 },
                                        { "mixolydian", 4 }, { "aeolian", 5 }, { "ionian", 0 } })
                all.push_back ({ name, name, "church mode", pythagorean (start), 0 });
            for (auto [name, start] : { std::pair { "hypodorian", 5 }, { "hypophrygian", 6 }, { "hypolydian", 0 },
                                        { "hypomixolydian", 1 }, { "hypoaeolian", 2 }, { "hypoionian", 4 } })
                all.push_back ({ name, name, "hypo-mode", pythagorean (start), 0 });

            const auto jazz = [&] (std::string name, std::string plain, std::initializer_list<int> steps)
            { all.push_back ({ std::move (name), std::move (plain), "jazz mode", semitones (steps), 0 }); };
            jazz ("melodic minor", "melodic minor", { 0, 2, 3, 5, 7, 9, 11 });
            jazz ("dorian ♭2", "dorian b2", { 0, 1, 3, 5, 7, 9, 10 });
            jazz ("lydian augmented", "lydian augmented", { 0, 2, 4, 6, 8, 9, 11 });
            jazz ("lydian dominant", "lydian dominant", { 0, 2, 4, 6, 7, 9, 10 });
            jazz ("mixolydian ♭6", "mixolydian b6", { 0, 2, 4, 5, 7, 8, 10 });
            jazz ("locrian ♮2", "locrian natural 2", { 0, 2, 3, 5, 6, 8, 10 });
            jazz ("altered", "altered", { 0, 1, 3, 4, 6, 8, 10 });
            jazz ("bebop dominant", "bebop dominant", { 0, 2, 4, 5, 7, 9, 10, 11 });
            jazz ("bebop major", "bebop major", { 0, 2, 4, 5, 7, 8, 9, 11 });
            jazz ("whole tone", "whole tone", { 0, 2, 4, 6, 8, 10 });
            jazz ("diminished (half-whole)", "diminished half-whole", { 0, 1, 3, 4, 6, 7, 9, 10 });
            jazz ("diminished (whole-half)", "diminished whole-half", { 0, 2, 3, 5, 6, 8, 9, 11 });
            jazz ("blues", "blues", { 0, 3, 5, 6, 7, 10 });

            // Olivier Messiaen's seven modes of limited transposition (Technique de mon langage
            // musical, 1944): each is made of a group of steps repeated, so after a few
            // transpositions it comes back onto its own notes (mode 1 has 2, mode 2 has 3, modes 3
            // to 7 have 4 or 6). In 12-TET, as he wrote them, each from its first note.
            const auto messiaen = [&] (std::string name, int mode, std::initializer_list<int> steps)
            { all.push_back ({ std::move (name), "messiaen mode " + std::to_string (mode), "messiaen mode", semitones (steps), 0 }); };
            messiaen ("messiaen mode 1 (whole tone)", 1, { 0, 2, 4, 6, 8, 10 });
            messiaen ("messiaen mode 2 (octatonic)", 2, { 0, 1, 3, 4, 6, 7, 9, 10 });
            messiaen ("messiaen mode 3", 3, { 0, 2, 3, 4, 6, 7, 8, 10, 11 });
            messiaen ("messiaen mode 4", 4, { 0, 1, 2, 5, 6, 7, 8, 11 });
            messiaen ("messiaen mode 5", 5, { 0, 1, 5, 6, 7, 11 });
            messiaen ("messiaen mode 6", 6, { 0, 2, 4, 5, 6, 8, 10, 11 });
            messiaen ("messiaen mode 7", 7, { 0, 1, 2, 3, 5, 6, 7, 8, 9, 11 });

            // Maqams (IJMES) and dastgahs, a quarter step being 50 cents.
            const auto cents50 = [&] (std::string name, std::string plain, const char* family, std::vector<double> c)
            { all.push_back ({ std::move (name), std::move (plain), family, std::move (c), 0 }); };
            cents50 ("rāst", "rast", "maqam", { 0, 200, 350, 500, 700, 900, 1050 });
            cents50 ("bayātī", "bayati", "maqam", { 0, 150, 300, 500, 700, 800, 1000 });
            cents50 ("sīkāh", "sikah", "maqam", { 0, 150, 350, 550, 700, 850, 1050 });
            cents50 ("ḥijāz", "hijaz", "maqam", { 0, 100, 400, 500, 700, 800, 1000 });
            cents50 ("ṣabā", "saba", "maqam", { 0, 150, 300, 400, 700, 800, 1000 });
            cents50 ("nahāwand", "nahawand", "maqam", { 0, 200, 300, 500, 700, 800, 1100 });
            cents50 ("kurd", "kurd", "maqam", { 0, 100, 300, 500, 700, 800, 1000 });
            cents50 ("ʿajam", "ajam", "maqam", { 0, 200, 400, 500, 700, 900, 1100 });
            cents50 ("huzām", "huzam", "maqam", { 0, 150, 350, 450, 750, 850, 1050 });
            cents50 ("šur", "shur", "dastgah", { 0, 150, 300, 500, 700, 800, 1000 });
            cents50 ("homāyun", "homayun", "dastgah", { 0, 150, 400, 500, 700, 800, 1000 });
            cents50 ("segāh", "segah", "dastgah", { 0, 150, 350, 550, 700, 850, 1050 });
            cents50 ("čahārgāh", "chahargah", "dastgah", { 0, 150, 400, 500, 700, 850, 1100 });
            cents50 ("māhur", "mahur", "dastgah", { 0, 200, 400, 500, 700, 900, 1100 });
            cents50 ("navā", "nava", "dastgah", { 0, 200, 350, 500, 700, 800, 1000 });
            cents50 ("rāst-panjgāh", "rast-panjgah", "dastgah", { 0, 200, 400, 500, 700, 900, 1000 });

            for (int n = 2; n <= 21; ++n)
            {
                const auto name = std::to_string (n) + " equal steps";
                std::vector<double> c;
                for (int i = 0; i < n; ++i)
                    c.push_back (1200.0 * i / n);
                all.push_back ({ name, name, "equal", std::move (c), n });
            }
            return all;
        }
    }

    const std::vector<TuningSystem>& tuningSystems()
    {
        static const auto all = build();
        return all;
    }

    int findTuning (const std::string& plain)
    {
        const auto& all = tuningSystems();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].plain == plain)
                return (int) i;
        return -1;
    }

    uint16_t modeKeys (const TuningSystem& t)
    {
        uint16_t keys = 0;
        for (auto c : t.cents)
            keys |= (uint16_t) (1u << (((int) std::floor (c / 100.0 + 0.5) % 12 + 12) % 12));
        return keys;
    }

    std::array<double, 12> keyCents (const TuningSystem& t)
    {
        std::array<double, 12> out;
        bool taken[12] {};
        for (int k = 0; k < 12; ++k)
            out[(size_t) k] = k * 100.0;
        for (auto c : t.cents)
        {
            const auto key = ((int) std::floor (c / 100.0 + 0.5) % 12 + 12) % 12;
            if (taken[key])
                continue;
            taken[key] = true;
            out[(size_t) key] = c;
        }
        return out;
    }

    phi::Tuning makeTuning (const TuningSystem& t, int root)
    {
        root = ((root % 12) + 12) % 12;
        const int rootKey = 60 + root;
        const double rootHz = 440.0 * std::pow (2.0, (rootKey - 69) / 12.0);
        if (t.divisions > 0)
            return phi::Tuning::equal (t.divisions, rootKey, rootHz);
        const auto keys = keyCents (t);
        return phi::Tuning::scale (std::vector<double> (keys.begin(), keys.end()), 1200.0, rootKey, rootHz);
    }

    TuningChoice tuningInForce (const TuningChoice& clip, const TuningChoice& scene, const TuningChoice& track)
    {
        if (clip.system >= 0)
            return clip;
        if (scene.system >= 0)
            return scene;
        return track;
    }

    bool hypoFamily (const std::string& family)
    {
        for (const char* f : { "raga", "raga, equal tempered", "maqam", "dastgah", "Raga" })
            if (family == f)
                return true;
        return false;
    }

    bool hasHypo (const std::vector<double>& degrees, Hypo h)
    {
        if (h == Hypo::none)
            return true;
        const auto want = h == Hypo::fifth ? cents (3.0 / 2) : cents (4.0 / 3);
        return std::any_of (degrees.begin(), degrees.end(), [want] (double c) { return std::abs (c - want) <= 30.0; });
    }

    bool hasHypo (uint16_t notes, Hypo h)
    {
        return h == Hypo::none || (notes & (1u << (h == Hypo::fifth ? 7 : 5))) != 0;
    }

    bool hasHypo (const TuningSystem& t, Hypo h)
    {
        return h == Hypo::none || (t.divisions == 0 && hypoFamily (t.family) && hasHypo (t.cents, h));
    }

    int hypoBelow (Hypo h)
    {
        return h == Hypo::fifth ? 5 : h == Hypo::fourth ? 7 : 0;
    }
}
