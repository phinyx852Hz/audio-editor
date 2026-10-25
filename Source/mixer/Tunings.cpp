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
            TuningSystem t { std::move (name), std::move (plain), "Raga", {}, 0 };
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
            // Darbārī's ati-komal ga and dha, Toḍī's and Bhairav's low re and dha.
            const auto r = [&] (const char* name, const char* plain, std::initializer_list<int> svaras,
                                std::initializer_list<std::pair<int, double>> bent = {})
            { all.push_back (raga (name, plain, svaras, bent)); };
            const double re81 = 256.0 / 243, ga27 = 32.0 / 27, dha81 = 128.0 / 81;
            r ("Bilāval", "Bilaval", { 0, 2, 4, 5, 7, 9, 11 });
            r ("Yaman (Kalyāṇ)", "Yaman (Kalyan)", { 0, 2, 4, 6, 7, 9, 11 });
            r ("Khamāj", "Khamaj", { 0, 2, 4, 5, 7, 9, 10 });
            r ("Kāfī", "Kafi", { 0, 2, 3, 5, 7, 9, 10 });
            r ("Āsāvarī", "Asavari", { 0, 2, 3, 5, 7, 8, 10 });
            r ("Bhairavī", "Bhairavi", { 0, 1, 3, 5, 7, 8, 10 });
            r ("Bhairav", "Bhairav", { 0, 1, 4, 5, 7, 8, 11 }, { { 1, re81 }, { 8, dha81 } });
            r ("Mārvā", "Marva", { 0, 1, 4, 6, 9, 11 });
            r ("Pūrvī", "Purvi", { 0, 1, 4, 6, 7, 8, 11 });
            r ("Toḍī", "Todi", { 0, 1, 3, 6, 7, 8, 11 }, { { 1, re81 }, { 3, ga27 }, { 6, 729.0 / 512 }, { 8, dha81 } });
            r ("Darbārī Kānaḍā", "Darbari Kanada", { 0, 2, 3, 5, 7, 8, 10 }, { { 3, ga27 }, { 8, dha81 } });
            r ("Jaunpurī", "Jaunpuri", { 0, 2, 3, 5, 7, 8, 10 });
            r ("Bāgeśrī", "Bageshri", { 0, 2, 3, 5, 7, 9, 10 });
            r ("Bhīmpalāsī", "Bhimpalasi", { 0, 2, 3, 5, 7, 9, 10 });
            r ("Deś", "Desh", { 0, 2, 4, 5, 7, 9, 10, 11 });
            r ("Bihāg", "Bihag", { 0, 2, 4, 5, 6, 7, 9, 11 });
            r ("Kedār", "Kedar", { 0, 2, 4, 5, 6, 7, 9, 11 });
            r ("Miyāṅ kī Malhār", "Miyan ki Malhar", { 0, 2, 3, 5, 7, 9, 10, 11 });
            r ("Megh", "Megh", { 0, 2, 5, 7, 10 });
            r ("Bhūpālī", "Bhupali", { 0, 2, 4, 7, 9 });
            r ("Durgā", "Durga", { 0, 2, 5, 7, 9 });
            r ("Haṃsadhvani", "Hamsadhvani", { 0, 2, 4, 7, 11 });
            r ("Mālkauns", "Malkauns", { 0, 3, 5, 8, 10 });
            r ("Jog", "Jog", { 0, 3, 4, 5, 7, 10 });
            r ("Ahīr Bhairav", "Ahir Bhairav", { 0, 1, 4, 5, 7, 9, 10 });
            r ("Lalit", "Lalit", { 0, 1, 4, 5, 6, 8, 11 });
            r ("Pūriyā Dhanāśrī", "Puriya Dhanashri", { 0, 1, 4, 6, 7, 8, 11 });
            r ("Śrī", "Shri", { 0, 1, 4, 6, 7, 8, 11 });
            r ("Multānī", "Multani", { 0, 1, 3, 6, 7, 8, 11 });
            r ("Cārukeśī", "Charukeshi", { 0, 2, 4, 5, 7, 8, 10 });

            // Church modes, from their final. A hypo-mode spans the octave a fourth below its
            // final, so its root here is the bottom of that range (Hypodorian on A for a D final).
            for (auto [name, start] : { std::pair { "Dorian", 1 }, { "Phrygian", 2 }, { "Lydian", 3 },
                                        { "Mixolydian", 4 }, { "Aeolian", 5 }, { "Ionian", 0 } })
                all.push_back ({ name, name, "Church mode", pythagorean (start), 0 });
            for (auto [name, start] : { std::pair { "Hypodorian", 5 }, { "Hypophrygian", 6 }, { "Hypolydian", 0 },
                                        { "Hypomixolydian", 1 }, { "Hypoaeolian", 2 }, { "Hypoionian", 4 } })
                all.push_back ({ name, name, "Hypo-mode", pythagorean (start), 0 });

            const auto jazz = [&] (std::string name, std::string plain, std::initializer_list<int> steps)
            { all.push_back ({ std::move (name), std::move (plain), "Jazz mode", semitones (steps), 0 }); };
            jazz ("Melodic minor", "Melodic minor", { 0, 2, 3, 5, 7, 9, 11 });
            jazz ("Dorian ♭2", "Dorian b2", { 0, 1, 3, 5, 7, 9, 10 });
            jazz ("Lydian augmented", "Lydian augmented", { 0, 2, 4, 6, 8, 9, 11 });
            jazz ("Lydian dominant", "Lydian dominant", { 0, 2, 4, 6, 7, 9, 10 });
            jazz ("Mixolydian ♭6", "Mixolydian b6", { 0, 2, 4, 5, 7, 8, 10 });
            jazz ("Locrian ♮2", "Locrian natural 2", { 0, 2, 3, 5, 6, 8, 10 });
            jazz ("Altered", "Altered", { 0, 1, 3, 4, 6, 8, 10 });
            jazz ("Bebop dominant", "Bebop dominant", { 0, 2, 4, 5, 7, 9, 10, 11 });
            jazz ("Bebop major", "Bebop major", { 0, 2, 4, 5, 7, 8, 9, 11 });
            jazz ("Whole tone", "Whole tone", { 0, 2, 4, 6, 8, 10 });
            jazz ("Diminished (half-whole)", "Diminished half-whole", { 0, 1, 3, 4, 6, 7, 9, 10 });
            jazz ("Diminished (whole-half)", "Diminished whole-half", { 0, 2, 3, 5, 6, 8, 9, 11 });
            jazz ("Blues", "Blues", { 0, 3, 5, 6, 7, 10 });

            // Maqams (IJMES) and dastgahs, a quarter step being 50 cents.
            const auto cents50 = [&] (std::string name, std::string plain, const char* family, std::vector<double> c)
            { all.push_back ({ std::move (name), std::move (plain), family, std::move (c), 0 }); };
            cents50 ("Rāst", "Rast", "Maqam", { 0, 200, 350, 500, 700, 900, 1050 });
            cents50 ("Bayātī", "Bayati", "Maqam", { 0, 150, 300, 500, 700, 800, 1000 });
            cents50 ("Sīkāh", "Sikah", "Maqam", { 0, 150, 350, 550, 700, 850, 1050 });
            cents50 ("Ḥijāz", "Hijaz", "Maqam", { 0, 100, 400, 500, 700, 800, 1000 });
            cents50 ("Ṣabā", "Saba", "Maqam", { 0, 150, 300, 400, 700, 800, 1000 });
            cents50 ("Nahāwand", "Nahawand", "Maqam", { 0, 200, 300, 500, 700, 800, 1100 });
            cents50 ("Kurd", "Kurd", "Maqam", { 0, 100, 300, 500, 700, 800, 1000 });
            cents50 ("ʿAjam", "Ajam", "Maqam", { 0, 200, 400, 500, 700, 900, 1100 });
            cents50 ("Huzām", "Huzam", "Maqam", { 0, 150, 350, 450, 750, 850, 1050 });
            cents50 ("Šur", "Shur", "Dastgah", { 0, 150, 300, 500, 700, 800, 1000 });
            cents50 ("Homāyun", "Homayun", "Dastgah", { 0, 150, 400, 500, 700, 800, 1000 });
            cents50 ("Segāh", "Segah", "Dastgah", { 0, 150, 350, 550, 700, 850, 1050 });
            cents50 ("Čahārgāh", "Chahargah", "Dastgah", { 0, 150, 400, 500, 700, 850, 1100 });
            cents50 ("Māhur", "Mahur", "Dastgah", { 0, 200, 400, 500, 700, 900, 1100 });
            cents50 ("Navā", "Nava", "Dastgah", { 0, 200, 350, 500, 700, 800, 1000 });
            cents50 ("Rāst-Panjgāh", "Rast-Panjgah", "Dastgah", { 0, 200, 400, 500, 700, 900, 1000 });

            for (int n = 2; n <= 21; ++n)
            {
                const auto name = std::to_string (n) + " equal steps";
                std::vector<double> c;
                for (int i = 0; i < n; ++i)
                    c.push_back (1200.0 * i / n);
                all.push_back ({ name, name, "Equal", std::move (c), n });
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
