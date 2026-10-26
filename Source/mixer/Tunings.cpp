// SPDX-License-Identifier: AGPL-3.0-only
#include "Tunings.h"

#include "../core/PhiRaga.h"

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

        // A scale read from its `start`th degree, as a new tonic.
        std::vector<double> rotate (const std::vector<double>& c, size_t start)
        {
            std::vector<double> out;
            for (size_t i = 0; i < c.size(); ++i)
            {
                auto d = c[(start + i) % c.size()] - c[start];
                out.push_back (d < 0 ? d + 1200.0 : d);
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

            // Shona mbira (owner, 2026-10-26: "mode and tuning systems from Zimbabwe, especially from
            // the Shona people"). Mbira dzavadzimu tunings are one heptatonic gamut read from
            // different keys, as Garikayi Tirikoti's mbira orchestra plays one scale from seven
            // starting notes: nyamaropa is near Mixolydian, dambatsoko (the Mujuru family's) near
            // Ionian, katsanzaira near Dorian, mavembe or gandanga near Phrygian, dindinyongwe
            // near Locrian. The gamut is a Shona mbira dzavadzimu in nyamaropa's shape measured by
            // McNeil and Mitran (J. Acoust. Soc. Am. 123, 2008), steps of 177, 193, 125, 190, 195,
            // 135 and 185 cents; the others are its rotations.
            const auto mbira = [&] (std::string name, std::string plain, const char* family, std::vector<double> c)
            { all.push_back ({ std::move (name), std::move (plain), family, std::move (c), 0 }); };
            const std::vector<double> nyamaropa { 0, 177, 370, 495, 685, 880, 1015 };
            mbira ("nyamaropa", "nyamaropa", "mbira dzavadzimu", nyamaropa);
            mbira ("dambatsoko", "dambatsoko", "mbira dzavadzimu", rotate (nyamaropa, 3));
            mbira ("katsanzaira", "katsanzaira", "mbira dzavadzimu", rotate (nyamaropa, 4));
            mbira ("mavembe (gandanga)", "mavembe (gandanga)", "mbira dzavadzimu", rotate (nyamaropa, 5));
            mbira ("dindinyongwe", "dindinyongwe", "mbira dzavadzimu", rotate (nyamaropa, 2));
            // A player's own nyamaropa, from Berliner's measurements (The Soul of Mbira, 1978, via
            // Rechberger's Scales and Modes Around the World): wider seconds, a low seventh.
            mbira ("nyamaropa (berliner)", "nyamaropa (berliner)", "mbira dzavadzimu", { 0, 196, 377, 506, 676, 877, 1050 });
            // Saungweme (the maker Simboti Mukuwurirwa's): a flattened whole tone, close to seven
            // equal steps, here exactly so.
            {
                std::vector<double> c;
                for (int i = 0; i < 7; ++i)
                    c.push_back (1200.0 * i / 7);
                mbira ("saungweme", "saungweme", "mbira dzavadzimu", std::move (c));
            }
            // Nyunga nyunga (the 15-key karimba of Manicaland, taught at Kwanongoma): six tones, the
            // gamut without its fourth. Kwanongoma's instruments are in F, in 12-TET; the other
            // is dambatsoko's gamut without its fourth.
            mbira ("nyunga nyunga (kwanongoma)", "nyunga nyunga (kwanongoma)", "nyunga nyunga", { 0, 200, 400, 700, 900, 1100 });
            {
                auto c = rotate (nyamaropa, 3);
                c.erase (c.begin() + 3);
                mbira ("nyunga nyunga", "nyunga nyunga", "nyunga nyunga", std::move (c));
            }
            // Matepe (hera), of the Korekore and Sena on the Mozambique border: Saini Madera's
            // instrument as Andrew Tracey measured it in June 1969 (African Music 4(4), 1970), its
            // right hand from C at 216 vibrations a second, near seven equal steps.
            {
                std::vector<double> c;
                for (double hz : { 216.0, 240.0, 266.0, 288.0, 320.0, 352.0, 392.0 })
                    c.push_back (cents (hz / 216.0));
                mbira ("matepe (saini madera)", "matepe (saini madera)", "matepe", std::move (c));
            }

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
