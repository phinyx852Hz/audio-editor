// SPDX-License-Identifier: AGPL-3.0-only
#include "Tunings.h"

#include <cmath>

namespace mixer
{
    namespace
    {
        double cents (double ratio) { return phi::Tuning::ratioToCents (ratio); }

        // A raga in just intonation: its svaras (semitones above Sa), each at the five-limit
        // ratio Hindustani practice gives it, unless the raga bends it (`bent`, semitone -> ratio).
        TuningSystem raga (std::string name, std::string plain, std::initializer_list<int> svaras,
                           std::initializer_list<std::pair<int, double>> bent = {})
        {
            static const double just[12] { 1.0, 16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3,
                                           45.0 / 32, 3.0 / 2, 8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8 };
            TuningSystem t { std::move (name), std::move (plain), "raga", {}, 0 };
            for (auto s : svaras)
            {
                auto ratio = just[s];
                for (auto& [at, r] : bent)
                    if (at == s)
                        ratio = r;
                t.cents.push_back (cents (ratio));
            }
            return t;
        }

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
            // Ragas (IAST). Komal svaras that the raga sings low take their Pythagorean ratio:
            // darbārī's ati-komal ga and dha, the kānaḍās' ga, toḍī's and bhairav's low re and dha.
            const auto r = [&] (const char* name, const char* plain, std::initializer_list<int> svaras,
                                std::initializer_list<std::pair<int, double>> bent = {})
            { all.push_back (raga (name, plain, svaras, bent)); };
            const double re81 = 256.0 / 243, ga27 = 32.0 / 27, dha81 = 128.0 / 81;
            r ("bilāval", "bilaval", { 0, 2, 4, 5, 7, 9, 11 });
            r ("yaman (kalyāṇ)", "yaman (kalyan)", { 0, 2, 4, 6, 7, 9, 11 });
            r ("khamāj", "khamaj", { 0, 2, 4, 5, 7, 9, 10 });
            r ("kāfī", "kafi", { 0, 2, 3, 5, 7, 9, 10 });
            r ("āsāvarī", "asavari", { 0, 2, 3, 5, 7, 8, 10 });
            r ("bhairavī", "bhairavi", { 0, 1, 3, 5, 7, 8, 10 });
            r ("bhairav", "bhairav", { 0, 1, 4, 5, 7, 8, 11 }, { { 1, re81 }, { 8, dha81 } });
            r ("mārvā", "marva", { 0, 1, 4, 6, 9, 11 });
            r ("pūrvī", "purvi", { 0, 1, 4, 6, 7, 8, 11 });
            r ("toḍī", "todi", { 0, 1, 3, 6, 7, 8, 11 }, { { 1, re81 }, { 3, ga27 }, { 6, 729.0 / 512 }, { 8, dha81 } });
            r ("darbārī kānaḍā", "darbari kanada", { 0, 2, 3, 5, 7, 8, 10 }, { { 3, ga27 }, { 8, dha81 } });
            r ("jaunpurī", "jaunpuri", { 0, 2, 3, 5, 7, 8, 10 });
            r ("bāgeśrī", "bageshri", { 0, 2, 3, 5, 7, 9, 10 });
            r ("bhīmpalāsī", "bhimpalasi", { 0, 2, 3, 5, 7, 9, 10 });
            r ("deś", "desh", { 0, 2, 4, 5, 7, 9, 10, 11 });
            r ("bihāg", "bihag", { 0, 2, 4, 5, 6, 7, 9, 11 });
            r ("kedār", "kedar", { 0, 2, 4, 5, 6, 7, 9, 11 });
            r ("miyāṅ kī malhār", "miyan ki malhar", { 0, 2, 3, 5, 7, 9, 10, 11 });
            r ("megh", "megh", { 0, 2, 5, 7, 10 });
            r ("bhūpālī", "bhupali", { 0, 2, 4, 7, 9 });
            r ("durgā", "durga", { 0, 2, 5, 7, 9 });
            r ("haṃsadhvani", "hamsadhvani", { 0, 2, 4, 7, 11 });
            r ("mālkauns", "malkauns", { 0, 3, 5, 8, 10 });
            r ("jog", "jog", { 0, 3, 4, 5, 7, 10 });
            r ("jogkauns", "jogkauns", { 0, 3, 4, 5, 8, 10 });   // jog's two ga over mālkauns
            r ("ahīr bhairav", "ahir bhairav", { 0, 1, 4, 5, 7, 9, 10 });
            r ("lalit", "lalit", { 0, 1, 4, 5, 6, 8, 11 });
            r ("pūriyā dhanāśrī", "puriya dhanashri", { 0, 1, 4, 6, 7, 8, 11 });
            r ("śrī", "shri", { 0, 1, 4, 6, 7, 8, 11 });
            r ("kaunsī kānaḍā", "kaunsi kanada", { 0, 2, 3, 5, 7, 8, 10 }, { { 3, ga27 } });   // mālkauns with the kānaḍā ga
            // More of the kānaḍā family (owner, 2026-10-25), each with the kānaḍā ga.
            r ("nāyakī kānaḍā", "nayaki kanada", { 0, 2, 3, 5, 7, 10 }, { { 3, ga27 } });
            r ("kāfī kānaḍā", "kafi kanada", { 0, 2, 3, 5, 7, 9, 10 }, { { 3, ga27 } });
            r ("śahānā kānaḍā", "shahana kanada", { 0, 2, 3, 5, 7, 9, 10 }, { { 3, ga27 } });
            r ("cārukeśī", "charukeshi", { 0, 2, 4, 5, 7, 8, 10 });
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
