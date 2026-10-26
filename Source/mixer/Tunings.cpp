// SPDX-License-Identifier: AGPL-3.0-only
#include "Tunings.h"

#include "../core/PhiRaga.h"

#include <algorithm>
#include <cmath>
#include <map>

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

            // Indonesian gamelan. No two gamelan are tuned alike, so each laras is an average of
            // measured sets: Javanese sléndro and pélog from Surjodiningrat, Sudarjana and Susanto's
            // survey of the Central Javanese gamelan (1972), Balinese selisir from gong kebyar. A
            // pathet or patutan is the laras's tones in use, read from a gong tone it rests on.
            // Tones are numbered as the kepatihan notation numbers them (sléndro has no 4).
            const std::map<int, double> slendro { { 1, 0.0 }, { 2, 231.0 }, { 3, 474.0 }, { 5, 717.0 }, { 6, 955.0 } };
            const std::map<int, double> pelog { { 1, 0.0 }, { 2, 120.0 }, { 3, 258.0 }, { 4, 539.0 },
                                                { 5, 675.0 }, { 6, 785.0 }, { 7, 943.0 } };
            const auto laras = [&] (std::string name, std::string plain, const char* family, const std::map<int, double>& tones,
                                    std::initializer_list<int> use, int from)
            {
                std::vector<double> c;
                for (auto tone : use)
                {
                    auto d = tones.at (tone) - tones.at (from);
                    c.push_back (d < 0 ? d + 1200.0 : d);
                }
                std::sort (c.begin(), c.end());
                all.push_back ({ std::move (name), std::move (plain), family, std::move (c), 0 });
            };
            laras ("sléndro", "slendro", "javanese gamelan", slendro, { 1, 2, 3, 5, 6 }, 1);
            laras ("sléndro pathet nem", "slendro pathet nem", "javanese gamelan", slendro, { 1, 2, 3, 5, 6 }, 2);
            laras ("sléndro pathet sanga", "slendro pathet sanga", "javanese gamelan", slendro, { 1, 2, 3, 5, 6 }, 5);
            laras ("sléndro pathet manyura", "slendro pathet manyura", "javanese gamelan", slendro, { 1, 2, 3, 5, 6 }, 6);
            laras ("pélog", "pelog", "javanese gamelan", pelog, { 1, 2, 3, 4, 5, 6, 7 }, 1);
            laras ("pélog pathet lima", "pelog pathet lima", "javanese gamelan", pelog, { 1, 2, 3, 5, 6 }, 1);
            laras ("pélog pathet nem", "pelog pathet nem", "javanese gamelan", pelog, { 1, 2, 3, 5, 6 }, 6);
            laras ("pélog pathet barang", "pelog pathet barang", "javanese gamelan", pelog, { 2, 3, 5, 6, 7 }, 2);
            // Bali: gong kebyar's five-tone pélog (ding dong deng dung dang), then the patutan of the
            // seven-tone saih pitu of the semar pagulingan, read on the Javanese pélog above.
            const std::map<int, double> kebyar { { 1, 0.0 }, { 2, 128.0 }, { 3, 266.0 }, { 5, 674.0 }, { 6, 786.0 } };
            laras ("selisir", "selisir", "balinese gamelan", kebyar, { 1, 2, 3, 5, 6 }, 1);
            laras ("saih pitu", "saih pitu", "balinese gamelan", pelog, { 1, 2, 3, 4, 5, 6, 7 }, 1);
            laras ("patutan tembung", "patutan tembung", "balinese gamelan", pelog, { 4, 5, 6, 1, 2 }, 4);
            laras ("patutan sunaren", "patutan sunaren", "balinese gamelan", pelog, { 5, 6, 7, 2, 3 }, 5);
            // Sunda: salendro in its theory's five equal steps; degung, the Sundanese five-tone pélog,
            // in its common reading on the keys (da mi na ti la, numbered 1 to 5 from the top, as
            // C B G F E), from la upward.
            all.push_back ({ "salendro", "salendro", "sundanese gamelan", { 0, 240, 480, 720, 960 }, 0 });
            all.push_back ({ "degung", "degung", "sundanese gamelan", { 0, 100, 300, 700, 800 }, 0 });

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
}
