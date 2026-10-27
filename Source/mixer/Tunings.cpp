// SPDX-License-Identifier: AGPL-3.0-only
#include "Tunings.h"

#include "WorldModes.h"
#include "../core/PhiRaga.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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

        // Notes reached by sanfen sunyi (三分損益, "take away a third, add a third"): each is `n`
        // pure fifths (3/2) from gong, folded into the octave, and read from the `tonic`th fifth.
        std::vector<double> fifths (const std::vector<int>& notes, int tonic)
        {
            std::vector<double> out;
            for (auto n : notes)
            {
                auto c = std::fmod ((n - tonic) * cents (1.5), 1200.0);
                out.push_back (c < 0 ? c + 1200.0 : c);
            }
            std::sort (out.begin(), out.end());
            return out;
        }

        // The first `count` notes of the chain of fifths from huangzhong, in the octave.
        std::vector<double> chainOfFifths (int count)
        {
            std::vector<double> out;
            for (int k = 0; k < count; ++k)
                out.push_back (std::fmod (k * cents (1.5), 1200.0));
            std::sort (out.begin(), out.end());
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
            // Chinese tunings (docs/east-asian-tunings.md has the sources). The twelve lü by
            // sanfen sunyi (Lüshi chunqiu, "Yinlü"): eleven pure fifths up from huangzhong, so
            // zhonglü lands a Pythagorean comma above the fourth and does not lead back.
            const auto chinese = [&] (std::string name, std::string plain, std::vector<double> c, int divisions = 0)
            { all.push_back ({ std::move (name), std::move (plain), "chinese tuning", std::move (c), divisions }); };
            chinese ("shí'èr lǜ (sānfēn sǔnyì)", "shier lu (sanfen sunyi)", chainOfFifths (12));
            // Jing Fang's 60 lü (Xu Han shu, "Lüli zhi"): the chain carried on to sixty notes; the
            // 54th (seyu) is 3.6 cents above huangzhong. Played a degree a key, like equal steps.
            chinese ("jīng fáng liùshí lǜ", "jing fang 60 lu", chainOfFifths (60));
            // Cai Yuanding's 18 lü (Lülü xinshu): the twelve and six biànlǜ, six fifths more.
            chinese ("cài yuándìng shíbā lǜ", "cai yuanding 18 lu", chainOfFifths (18));
            // He Chengtian's xīnlǜ (c. 443, Sui shu, "Lüli zhi"): what the pipe a fifth above
            // zhonglü misses of half huangzhong's length, shared out in twelve equal parts, k parts
            // added to the kth lü of the chain, so the twelfth closes on the octave.
            {
                std::vector<double> c;
                double length = 81.0;   // huangzhong, 81 fen
                const double gap = 81.0 * (0.5 - std::pow (2.0, 18) / std::pow (3.0, 12));
                for (int k = 0; k < 12; ++k)
                {
                    c.push_back (1200.0 * std::log2 (81.0 / (length + k * gap / 12.0)));
                    length *= 2.0 / 3.0;          // sun: a third taken away, a fifth up
                    if (length < 40.5)
                        length *= 2.0;            // yi: a third added, back into the octave
                }
                std::sort (c.begin(), c.end());
                chinese ("hé chéngtiān xīnlǜ", "he chengtian xinlu", std::move (c));
            }
            // Zhu Zaiyu's xīnfǎ mìlǜ (Lüxue xinshuo, 1584; Lülü jingyi, 1596): twelve equal steps.
            chinese ("zhū zàiyù xīnfǎ mìlǜ", "zhu zaiyu xinfa milu", semitones ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }), 12);
            // Chúnlǜ, just intonation, the five notes as the qin's harmonics (hui) sound them.
            chinese ("chúnlǜ wǔshēng (qín)", "chunlu wusheng (qin)",
                     { 0.0, cents (9.0 / 8), cents (5.0 / 4), cents (3.0 / 2), cents (5.0 / 3) });

            // Chinese modes, in sanfen sunyi: the five notes and the mode on each (Guanzi, "Diyuan"),
            // then the three seven-note scales (yayue, qingyue, yanyue) on each of the five. A note
            // is its count of fifths from gong: gong 0, zhi 1, shang 2, yu 3, jue 4, biangong 5,
            // bianzhi 6, qingjue -1, run -2.
            const size_t firstChineseMode = all.size();
            const std::pair<const char*, const char*> modes[] { { "gōng", "gong" }, { "shāng", "shang" }, { "jué", "jue" },
                                                                 { "zhǐ", "zhi" }, { "yǔ", "yu" } };
            const int tonics[] { 0, 2, 4, 1, 3 };
            for (int m = 0; m < 5; ++m)
                all.push_back ({ modes[m].first, modes[m].second, "chinese mode", fifths ({ 0, 1, 2, 3, 4 }, tonics[m]), 0 });
            for (auto [scale, plain, notes] : { std::tuple { "yǎyuè", "yayue", std::vector<int> { 0, 1, 2, 3, 4, 5, 6 } },
                                                { "qīngyuè", "qingyue", { -1, 0, 1, 2, 3, 4, 5 } },
                                                { "yànyuè", "yanyue", { -2, -1, 0, 1, 2, 3, 4 } } })
                for (int m = 0; m < 5; ++m)
                    all.push_back ({ std::string (scale) + " " + modes[m].first, std::string (plain) + " " + modes[m].second,
                                     "chinese mode", fifths (notes, tonics[m]), 0 });
            // Each again in 12-TET, as conservatories and modern Chinese orchestras play them.
            for (size_t i = firstChineseMode, n = all.size(); i < n; ++i)
            {
                auto t = all[i];
                t.name += " (equal tempered)";
                t.plain += " (equal tempered)";
                t.family = "chinese mode, equal tempered";
                for (auto& c : t.cents)
                    c = 100.0 * std::round (c / 100.0);
                all.push_back (std::move (t));
            }
            // Regional modes: Chaozhou's light and heavy 3 and 6 (zheng: a "heavy" press raises la
            // and mi three quarters of a tone), and Cantonese yǐfǎn on sol, its si low and fa high.
            const auto regional = [&] (std::string name, std::string plain, std::vector<double> c)
            { all.push_back ({ std::move (name), std::move (plain), "chinese regional mode", std::move (c), 0 }); };
            regional ("qīngsān qīngliù (cháozhōu)", "qingsan qingliu (chaozhou)", { 0, 200, 400, 700, 900 });
            regional ("zhòngsān zhòngliù (cháozhōu)", "zhongsan zhongliu (chaozhou)", { 0, 200, 550, 700, 1050 });
            regional ("yǐfǎn (guǎngdōng)", "yifan (guangdong)", { 0, 350, 500, 700, 1050 });

            // Japanese: the six modes of gagaku (rokuchōshi), in sanbun son'eki (Pythagorean) tuning
            // as the twelve ritsu came from China. Ryo is gong's scale with its two exchange notes
            // (Lydian), ritsu is shang's (Dorian). Their customary finals: ichikotsu D, hyōjō E,
            // sōjō G, ōshiki A, banshiki B, taishiki E.
            for (auto [name, plain, ryo] : { std::tuple { "ichikotsuchō", "ichikotsucho", true }, { "hyōjō", "hyojo", false },
                                             { "sōjō", "sojo", true }, { "ōshikichō", "oshikicho", false },
                                             { "banshikichō", "banshikicho", false }, { "taishikichō", "taishikicho", true } })
                all.push_back ({ name, plain, "gagaku mode", ryo ? pythagorean (3) : pythagorean (1), 0 });
            // Folk and art-music scales, in 12-TET: Uehara Rokushirō's yō and in (1895), Koizumi
            // Fumio's four tetrachord scales (1958), and two koto tunings as scales.
            const auto japanese = [&] (std::string name, std::string plain, std::initializer_list<int> steps)
            { all.push_back ({ std::move (name), std::move (plain), "japanese mode", semitones (steps), 0 }); };
            japanese ("miyako-bushi (in)", "miyako-bushi (in)", { 0, 1, 5, 7, 8 });
            japanese ("in (ascending)", "in (ascending)", { 0, 1, 5, 7, 10 });
            japanese ("ritsu (yō)", "ritsu (yo)", { 0, 2, 5, 7, 9 });
            japanese ("min'yō", "min'yo", { 0, 3, 5, 7, 10 });
            japanese ("ryūkyū", "ryukyu", { 0, 4, 5, 7, 11 });
            japanese ("hirajōshi", "hirajoshi", { 0, 2, 3, 7, 8 });
            japanese ("iwato", "iwato", { 0, 1, 5, 6, 10 });

            // Korean (2026-10-26). The twelve yul of the Akhak kwebŏm (1493), by sambun sonik as
            // the Chinese lü, hwangjong customarily on E flat; the court's two modes, p'yŏngjo (on
            // sol) and kyemyŏnjo (on la), in the same tuning; and Lee Bo-hyung's folk-song tori
            // (regional modes) in 12-TET. Names in McCune-Reischauer, Revised Romanization plain.
            all.push_back ({ "sibi-yul (sambun sonik)", "sibi-yul (sambun sonik)", "korean tuning", chainOfFifths (12), 0 });
            all.push_back ({ "p'yŏngjo", "pyeongjo", "korean mode", fifths ({ 0, 1, 2, 3, 4 }, 1), 0 });
            all.push_back ({ "kyemyŏnjo", "gyemyeonjo", "korean mode", fifths ({ 0, 1, 2, 3, 4 }, 3), 0 });
            const auto tori = [&] (std::string name, std::string plain, std::initializer_list<int> steps)
            { all.push_back ({ std::move (name), std::move (plain), "korean folk mode", semitones (steps), 0 }); };
            tori ("kyŏngt'ori", "gyeongtori", { 0, 2, 5, 7, 9 });          // Seoul and Gyeonggi: sol la do re mi
            tori ("menarit'ori", "menaritori", { 0, 3, 5, 8, 10 });        // the east: mi sol la do re
            tori ("yukchabaegit'ori", "yukjabaegitori", { 0, 5, 7, 8 });   // Jeolla: mi la (si) do
            tori ("susimgat'ori", "susimgatori", { 0, 2, 5, 7, 10 });      // the northwest: re mi sol la do

            world::add (all);

            // Equal steps: 2 to 24 (Blackwood's 13 to 24), and the composers' larger ones (19
            // Yasser's, 31 Huygens's and Fokker's, 36 Busoni's sixth tones, 48 and 96 Carrillo's,
            // 53 the Turkish commas, 72 the twelfth tones and the Byzantine moria).
            std::vector<int> counts;
            for (int n = 2; n <= 24; ++n)
                counts.push_back (n);
            for (int n : { 31, 36, 41, 48, 53, 72, 96 })
                counts.push_back (n);
            for (int n : counts)
            {
                const auto name = std::to_string (n) + " equal steps";
                std::vector<double> c;
                for (int i = 0; i < n; ++i)
                    c.push_back (1200.0 * i / n);
                all.push_back ({ name, name, "equal", std::move (c), n });
            }
            world::place (all);
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
        for (auto c : octaveCents (t))
            keys |= (uint16_t) (1u << (((int) std::floor (c / 100.0 + 0.5) % 12 + 12) % 12));
        return keys;
    }

    std::array<double, 12> keyCents (const TuningSystem& t)
    {
        std::array<double, 12> out;
        bool taken[12] {};
        for (int k = 0; k < 12; ++k)
            out[(size_t) k] = k * 100.0;
        const auto degrees = octaveCents (t);
        if (degrees.size() == 12 && t.divisions == 0)   // a temperament or a 12-note just tuning: a degree a key, in its order
        {
            std::copy (degrees.begin(), degrees.end(), out.begin());
            return out;
        }
        for (auto c : degrees)
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
        // A tuning that repeats at another period (the tritave, Carlos's steps), or one of more
        // than twelve degrees: a degree a key. A mode that repeats at a fourth (Hajibeyov's
        // chains, the obikhod) is on the keys, like the others.
        if (t.cents.size() > 12 || (std::abs (t.period - 1200.0) > 1.0e-6 && (t.kinds & melodic) == 0))
            return phi::Tuning::scale (t.cents, t.period, rootKey, rootHz);
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
        for (const char* f : { "raga", "raga, equal tempered", "maqam", "dastgah", "Raga", "melakarta", "makam", "avaz" })
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
