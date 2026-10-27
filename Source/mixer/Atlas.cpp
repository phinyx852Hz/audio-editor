// SPDX-License-Identifier: AGPL-3.0-only
// The systems of the owner's Mode Research (2026-10-26) that the list did not have yet, and the
// atlas: who and where each system of the list comes from. Names are lower case (owner), in each
// tradition's usual scholarly romanization: ALA-LC for Greek and Russian, IJMES for Arabic,
// Encyclopædia Iranica's for Persian, modern Turkish and Azerbaijani spelling, IAST for
// Sanskrit and ISO 15919 for the Karnatak melakartas, Indonesian spelling for gamelan,
// Vietnamese with its tones, and the Ethiopian names as the Ethiopian Studies transliteration
// gives them. A plain ASCII spelling goes beside each, for searching and for saving.
#include "Atlas.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace mixer
{
    namespace
    {
        double cents (double ratio) { return phi::Tuning::ratioToCents (ratio); }
        const double fifth = 1200.0 * std::log2 (1.5);

        std::vector<double> ratios (std::initializer_list<double> r)
        {
            std::vector<double> out;
            for (auto x : r)
                out.push_back (cents (x));
            return out;
        }

        std::vector<double> semitones (std::initializer_list<int> steps)
        {
            std::vector<double> out;
            for (auto s : steps)
                out.push_back (s * 100.0);
            return out;
        }

        // A chain of fifths of `size` cents, from `low` fifths below the tonic to `high` above,
        // folded into the octave and sorted: Pythagorean tuning at a pure fifth, a meantone at a
        // narrowed one.
        std::vector<double> chain (int low, int high, double size = fifth)
        {
            std::vector<double> out;
            for (int k = -low; k <= high; ++k)
            {
                auto c = std::fmod (k * size, 1200.0);
                out.push_back (c < 0 ? c + 1200.0 : c);
            }
            std::sort (out.begin(), out.end());
            return out;
        }

        // The white notes in Pythagorean tuning, read from the `start`th (0 C .. 6 B).
        std::vector<double> pythagoreanFrom (int start)
        {
            const auto white = chain (1, 5);   // F C G D A E B, sorted: C D E F G A B
            std::vector<double> out;
            for (int i = 0; i < 7; ++i)
            {
                auto c = white[(size_t) ((start + i) % 7)] - white[(size_t) start];
                out.push_back (c < 0 ? c + 1200.0 : c);
            }
            return out;
        }

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

        // Steps of a whole unit (moria, commas, parts) added up into degrees, `per` to the octave.
        std::vector<double> units (std::initializer_list<double> steps, double per)
        {
            std::vector<double> out { 0.0 };
            double at = 0.0;
            for (auto s : steps)
            {
                at += s;
                if (at < per - 1.0e-9)
                    out.push_back (at * 1200.0 / per);
            }
            return out;
        }

        // A Greek genus: its tetrachord (three ratios, low to high, making a 4/3) twice, a 9/8
        // apart, as the octave E to E (the Dorian species) has it.
        std::vector<double> genus (double a, double b)
        {
            const double t[] { 1.0, a, a * b };
            std::vector<double> out;
            for (auto x : t)
                out.push_back (cents (x));
            out.push_back (cents (4.0 / 3));
            for (auto x : t)
                out.push_back (cents (1.5 * x));
            return out;
        }

        int findTuningIn (const std::vector<TuningSystem>& all, const std::string& plain)
        {
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].plain == plain)
                    return (int) i;
            return -1;
        }

        // ASCII for a plain name: the romanizations' marks taken off.
        std::string ascii (const std::string& s)
        {
            static const std::vector<std::pair<std::string, std::string>> marks {
                { "ā", "a" }, { "á", "a" }, { "à", "a" }, { "ạ", "a" }, { "ă", "a" }, { "â", "a" }, { "ä", "a" }, { "ǎ", "a" },
                { "ē", "e" }, { "é", "e" }, { "è", "e" }, { "ê", "e" }, { "ë", "e" }, { "ẹ", "e" }, { "ə", "e" }, { "ế", "e" },
                { "ī", "i" }, { "í", "i" }, { "ì", "i" }, { "î", "i" }, { "ï", "i" }, { "ı", "i" },
                { "ō", "o" }, { "ó", "o" }, { "ò", "o" }, { "ô", "o" }, { "ö", "o" }, { "ơ", "o" }, { "ồ", "o" },
                { "ū", "u" }, { "ú", "u" }, { "ù", "u" }, { "û", "u" }, { "ü", "u" }, { "ư", "u" }, { "ǔ", "u" },
                { "ṭ", "t" }, { "ḍ", "d" }, { "đ", "d" }, { "ṇ", "n" }, { "ṅ", "n" }, { "ñ", "n" }, { "ś", "s" }, { "ṣ", "s" },
                { "š", "sh" }, { "ş", "sh" }, { "č", "ch" }, { "ç", "ch" }, { "ṃ", "m" }, { "ḷ", "l" }, { "ḥ", "h" }, { "ṛ", "r" },
                { "ṟ", "r" }, { "ẓ", "z" }, { "ğ", "g" }, { "ʿ", "" }, { "ʾ", "" }, { "’", "'" }, { "–", "-" }, { "♭", "b" },
                { "♯", "#" }, { "ệ", "e" }, { "ắ", "a" }, { "ơ", "o" }, { "ờ", "o" }, { "ả", "a" },
            };
            auto out = s;
            for (const auto& [from, to] : marks)
                for (size_t at = out.find (from); at != std::string::npos; at = out.find (from, at + to.size()))
                    out.replace (at, from.size(), to);
            return out;
        }
    }

    namespace atlas
    {
        void add (std::vector<TuningSystem>& all)
        {
            // An entry, with where it is from and what kind it is.
            const auto put = [&] (std::string name, std::string plain, const char* family, std::vector<double> c,
                                  std::vector<std::string> from, unsigned kinds, std::string note = {}) -> TuningSystem&
            {
                all.push_back ({ std::move (name), std::move (plain), family, std::move (c), 0 });
                auto& t = all.back();
                t.from = std::move (from);
                t.kinds = kinds;
                t.note = std::move (note);
                return t;
            };
            const unsigned both = melodic | tuning;

            //==================================================================
            // Europe.
            // Ancient Greece: the seven octave species (Ptolemy's tonoi, Boethius's names) in the
            // diatonic genus, Pythagorean as the Greater Perfect System tuned it. The Greek names
            // run the other way from the church modes' (the Greek Dorian is E to E).
            for (auto [name, plain, start] : { std::tuple { "dōrios", "greek dorian", 2 }, { "phrygios", "greek phrygian", 1 },
                                               { "lydios", "greek lydian", 0 }, { "mixolydios", "greek mixolydian", 6 },
                                               { "hypodōrios", "greek hypodorian", 5 }, { "hypophrygios", "greek hypophrygian", 4 },
                                               { "hypolydios", "greek hypolydian", 3 } })
                put (name, plain, "greek octave species", pythagoreanFrom (start), { "ancient greece" }, melodic,
                     "an octave species (tonos) of the Greater Perfect System");
            // The three genera, each as its theorist divided the tetrachord (the octave E to E: a
            // tetrachord, the 9/8 disjunctive tone, the tetrachord again).
            const auto genusOf = [&] (std::string name, const char* who, double a, double b, double c, std::string note = {})
            {
                put (name, ascii (name), "greek genus", genus (a, b), { who, "ancient greece" }, both, std::move (note));
                (void) c;   // the third step, the rest of the 4/3
            };
            genusOf ("enharmonic (archytas)", "archytas", 28.0 / 27, 36.0 / 35, 5.0 / 4, "28/27, 36/35, 5/4");
            genusOf ("chromatic (archytas)", "archytas", 28.0 / 27, 243.0 / 224, 32.0 / 27, "28/27, 243/224, 32/27");
            genusOf ("diatonic (archytas)", "archytas", 28.0 / 27, 8.0 / 7, 9.0 / 8, "28/27, 8/7, 9/8");
            genusOf ("enharmonic (ptolemy)", "ptolemy", 46.0 / 45, 24.0 / 23, 5.0 / 4, "46/45, 24/23, 5/4");
            genusOf ("soft chromatic (ptolemy)", "ptolemy", 28.0 / 27, 15.0 / 14, 6.0 / 5, "chrōma malakon: 28/27, 15/14, 6/5");
            genusOf ("intense chromatic (ptolemy)", "ptolemy", 22.0 / 21, 12.0 / 11, 7.0 / 6, "chrōma syntonon: 22/21, 12/11, 7/6");
            genusOf ("soft diatonic (ptolemy)", "ptolemy", 21.0 / 20, 10.0 / 9, 8.0 / 7, "diatonon malakon: 21/20, 10/9, 8/7");
            genusOf ("tonic diatonic (ptolemy)", "ptolemy", 28.0 / 27, 8.0 / 7, 9.0 / 8, "diatonon toniaion: 28/27, 8/7, 9/8");
            genusOf ("ditonic diatonic (ptolemy)", "ptolemy", 256.0 / 243, 9.0 / 8, 9.0 / 8, "diatonon ditoniaion: 256/243, 9/8, 9/8");
            genusOf ("intense diatonic (ptolemy)", "ptolemy", 16.0 / 15, 9.0 / 8, 10.0 / 9, "diatonon syntonon, an early just intonation: 16/15, 9/8, 10/9");
            genusOf ("even diatonic (ptolemy)", "ptolemy", 12.0 / 11, 11.0 / 10, 10.0 / 9, "diatonon homalon: 12/11, 11/10, 10/9");
            // Aristoxenus measured by ear, in twelfths of a tone: a fourth is 30 parts, the octave 72.
            for (auto [name, a, b] : { std::tuple { "enharmonic (aristoxenus)", 3.0, 3.0 }, { "soft chromatic (aristoxenus)", 4.0, 4.0 },
                                       { "hemiolic chromatic (aristoxenus)", 4.5, 4.5 }, { "tonic chromatic (aristoxenus)", 6.0, 6.0 },
                                       { "soft diatonic (aristoxenus)", 6.0, 9.0 }, { "intense diatonic (aristoxenus)", 6.0, 12.0 } })
                put (name, ascii (name), "greek genus", units ({ a, b, 30.0 - a - b, 12.0, a, b, 30.0 - a - b }, 72.0),
                     { "aristoxenus", "ancient greece" }, both, "in twelfths of a tone, a fourth being 30");
            // The school of Pythagoras: twelve notes by pure fifths, E flat to G sharp.
            put ("pythagorean", "pythagorean", "historical tuning", chain (3, 8), { "pythagoras (school of)", "ancient greece" }, tuning,
                 "pure fifths (3/2), E flat to G sharp; the wolf fifth falls between them");

            // Byzantine chant: the eight ēchoi of the oktōēchos, in the 72 moria of the
            // Patriarchal Music Committee (1881), from Chrysanthos's genera: diatonic (12 10 8),
            // soft chromatic (8 14 8), hard chromatic (6 20 4) and enharmonic (12 12 6).
            const auto echos = [&] (std::string name, std::string plain, std::initializer_list<double> moria, const char* genusName)
            { put (std::move (name), std::move (plain), "byzantine echos", units (moria, 72.0), { "byzantine and greek orthodox chant" }, both, genusName); };
            echos ("ēchos prōtos (first)", "echos protos", { 10, 8, 12, 12, 10, 8, 12 }, "diatonic, on pa (D)");
            echos ("ēchos deuteros (second)", "echos deuteros", { 8, 14, 8, 12, 8, 14, 8 }, "soft chromatic, on di (G)");
            echos ("ēchos tritos (third)", "echos tritos", { 12, 12, 6, 12, 12, 12, 6 }, "enharmonic, on ga (F)");
            echos ("ēchos tetartos (fourth)", "echos tetartos", { 12, 10, 8, 12, 10, 8, 12 }, "diatonic, on di (G)");
            echos ("ēchos plagios prōtos (plagal first)", "echos plagios protos", { 10, 8, 12, 12, 10, 8, 12 }, "diatonic, on low pa (D)");
            echos ("ēchos plagios deuteros (plagal second)", "echos plagios deuteros", { 6, 20, 4, 12, 6, 20, 4 }, "hard chromatic, on pa (D)");
            echos ("ēchos barys (grave)", "echos barys", { 12, 12, 12, 6, 12, 12, 6 }, "enharmonic, on zo flat (B flat)");
            echos ("ēchos plagios tetartos (plagal fourth)", "echos plagios tetartos", { 12, 10, 8, 12, 12, 10, 8 }, "diatonic, on ni (C)");
            // Russian chant: the obikhod scale, trichords of two tones a semitone apart, so it
            // repeats at the fourth (G A B, C D E, F G A, B♭ C D), not the octave.
            put ("obikhodnyi zvukoriad", "obikhod scale", "russian chant", semitones ({ 0, 2, 4 }), { "russian orthodox (znamenny chant)" }, melodic,
                 "the obikhod scale, repeating at the fourth: G A B C D E F G A B♭").period = 500.0;

            // The Western church's modes from Guido to Glarean (the eight and their hypo-modes are
            // in Tunings.cpp): Guido's hexachord, and the Locrian Glarean described and rejected.
            put ("hexachord (ut re mi fa sol la)", "hexachord", "hexachord", { 0.0, cents (9.0 / 8), cents (81.0 / 64), cents (4.0 / 3), cents (1.5), cents (27.0 / 16) },
                 { "guido of arezzo", "western church (gregorian chant)" }, melodic, "durum on G, naturale on C, molle on F");
            put ("locrian (hyperaeolian)", "locrian", "church mode", pythagoreanFrom (6), { "heinrich glarean" }, melodic,
                 "on B; Glarean named it and rejected it as unusable");
            // Renaissance tunings: Ramos de Pareja's monochord (Musica practica, 1482), Zarlino's
            // syntonic diatonic (Le istitutioni harmoniche, 1558) and his 2/7-comma meantone.
            put ("ramos de pareja's monochord", "ramos de pareja", "historical tuning",
                 ratios ({ 1, 135.0 / 128, 10.0 / 9, 32.0 / 27, 5.0 / 4, 4.0 / 3, 45.0 / 32, 1.5, 128.0 / 81, 5.0 / 3, 16.0 / 9, 15.0 / 8 }),
                 { "bartolomé ramos de pareja" }, tuning, "Musica practica (1482): an early just intonation");
            put ("just intonation (zarlino's syntonic diatonic)", "zarlino just intonation", "historical tuning",
                 ratios ({ 1, 9.0 / 8, 5.0 / 4, 4.0 / 3, 1.5, 5.0 / 3, 15.0 / 8 }), { "gioseffo zarlino" }, both, "1 9/8 5/4 4/3 3/2 5/3 15/8");
            put ("2/7-comma meantone (zarlino)", "zarlino meantone", "temperament", chain (3, 8, fifth - 2.0 / 7.0 * cents (81.0 / 80)),
                 { "gioseffo zarlino" }, tuning, "fifths narrowed by 2/7 of the syntonic comma, E flat to G sharp, laid on C");
            // Well temperaments, for every key (laid on C, whatever the final).
            put ("werckmeister iii", "werckmeister iii", "temperament",
                 { 0, 90.225, 192.180, 294.135, 390.225, 498.045, 588.270, 696.090, 792.180, 888.270, 996.090, 1092.180 },
                 { "andreas werckmeister" }, tuning, "Musicalische Temperatur (1691): C G D A and B F# a quarter comma narrow");
            put ("kirnberger iii", "kirnberger iii", "temperament",
                 { 0, 90.225, 193.157, 294.135, 386.314, 498.045, 590.224, 696.578, 792.180, 889.735, 996.090, 1088.269 },
                 { "johann philipp kirnberger" }, tuning, "C G D A E a quarter comma narrow, the rest pure but F# C#");

            // Named scales of the 19th and 20th centuries, in 12-TET as written.
            const auto scale = [&] (std::string name, std::initializer_list<int> steps, std::vector<std::string> from, std::string note = {})
            { put (name, ascii (name), "composer scale", semitones (steps), std::move (from), melodic, std::move (note)); };
            scale ("harmonic minor", { 0, 2, 3, 5, 7, 8, 11 }, { "tonal western music", "camille saint-saëns" }, "Saint-Saëns's \"exotic\" minor (Samson et Dalila)");
            scale ("hungarian minor", { 0, 2, 3, 6, 7, 8, 11 }, { "franz liszt", "hungary" }, "the \"gypsy\" scale: harmonic minor with a raised 4th");
            scale ("scala enigmatica", { 0, 1, 4, 6, 8, 10, 11 }, { "giuseppe verdi" }, "Adolfo Crescentini's (1888); Verdi's Ave Maria, Quattro pezzi sacri");
            scale ("prometheus (mystic chord)", { 0, 2, 4, 6, 9, 10 }, { "alexander scriabin" }, "Prometheus (1910)");
            scale ("pentatonic (major)", { 0, 2, 4, 7, 9 }, { "claude debussy", "maurice ravel" }, "Pagodes; Laideronnette from Ma mère l'oye");
            scale ("pentatonic (minor)", { 0, 3, 5, 7, 10 }, { "zoltán kodály", "hungary" }, "the Hungarian peasant pentatonic");
            scale ("lydian diminished", { 0, 2, 3, 6, 7, 9, 11 }, { "george russell" }, "one of the Lydian Chromatic Concept's principal scales (1953)");
            scale ("modal folksong scale (grainger)", { 0, 2, 4, 5, 7, 9, 10, 11 }, { "percy grainger" },
                   "1 2 3 4 5 ♭7 the core (the 3rd unstable), 6 and ♯7 passing notes");
            scale ("modo de mi (andalusian)", { 0, 1, 3, 4, 5, 7, 8, 10 }, { "spain (andalusia, flamenco)", "maurice ravel" },
                   "the Phrygian of flamenco, its 3rd both minor and major");

            // Annie Gilchrist's Gaelic modes (1911): five pentatonic modes on the frame F G A C D,
            // each filled out with E (form A) or B♭ (form B). Cecil Sharp kept her numbers for the
            // Southern Appalachians (1917) and swapped the letters.
            {
                const std::map<char, int> key { { 'C', 0 }, { 'D', 2 }, { 'E', 4 }, { 'F', 5 }, { 'G', 7 }, { 'A', 9 }, { 'b', 10 } };
                const struct { const char* penta; const char* a; const char* b; const char* full; } gaelic[] {
                    { "CDFGA", "CDEFGA", "CDFGAb", "mixolydian" }, { "DFGAC", "DEFGAC", "DFGAbC", "aeolian" },
                    { "FGACD", "FGACDE", "FGAbCD", "ionian" }, { "GACDF", "GACDEF", "GAbCDF", "dorian" },
                    { "ACDFG", "ACDEFG", "AbCDFG", "phrygian" } };
                for (int m = 0; m < 5; ++m)
                    for (auto [form, notes] : { std::pair { "", gaelic[m].penta }, { "a", gaelic[m].a }, { "b", gaelic[m].b } })
                    {
                        std::vector<double> c;
                        const int final = key.at (notes[0]);
                        for (const char* n = notes; *n != 0; ++n)
                            c.push_back (100.0 * ((key.at (*n) - final + 12) % 12));
                        std::sort (c.begin(), c.end());
                        const auto name = "gaelic mode " + std::to_string (m + 1) + (*form != 0 ? std::string (form) : std::string (" (pentatonic)"));
                        put (name, name, "gaelic mode", std::move (c), { "annie gilchrist", "cecil sharp", "british and irish folk", "southern appalachia" },
                             melodic, std::string ("on ") + notes[0] + (*form == 0 ? "; heptatonic: " + std::string (gaelic[m].full)
                                                                                    : *form == 'a' ? "; E filled in" : "; B♭ filled in"));
                    }
            }

            // Jewish prayer modes (nusach, shtayger), as Idelsohn set them out.
            const auto shtayger = [&] (std::string name, std::initializer_list<int> steps, std::string note)
            { put (name, ascii (name), "shtayger", semitones (steps), { "ashkenazi jewish liturgy (nusach)", "abraham zvi idelsohn" }, melodic, std::move (note)); };
            shtayger ("ahavah rabbah", { 0, 1, 4, 5, 7, 8, 10 }, "freygish; klezmer's most used mode");
            shtayger ("adonai malakh", { 0, 2, 4, 5, 7, 9, 10 }, "major with a lowered 7th");
            shtayger ("magen avot", { 0, 2, 3, 5, 7, 8, 10 }, "minor");
            shtayger ("mi she-berakh", { 0, 2, 3, 6, 7, 9, 10 }, "the \"altered Dorian\", with a raised 4th");

            // Microtonal and just tunings of the 20th century (the equal temperaments are with the
            // equal steps below, placed under their composers).
            put ("partch 43-tone just intonation", "partch 43", "just intonation",
                 ratios ({ 1, 81.0 / 80, 33.0 / 32, 21.0 / 20, 16.0 / 15, 12.0 / 11, 11.0 / 10, 10.0 / 9, 9.0 / 8, 8.0 / 7, 7.0 / 6,
                           32.0 / 27, 6.0 / 5, 11.0 / 9, 5.0 / 4, 14.0 / 11, 9.0 / 7, 21.0 / 16, 4.0 / 3, 27.0 / 20, 11.0 / 8, 7.0 / 5,
                           10.0 / 7, 16.0 / 11, 40.0 / 27, 1.5, 32.0 / 21, 14.0 / 9, 11.0 / 7, 8.0 / 5, 18.0 / 11, 5.0 / 3, 27.0 / 16,
                           12.0 / 7, 7.0 / 4, 16.0 / 9, 9.0 / 5, 20.0 / 11, 11.0 / 6, 15.0 / 8, 40.0 / 21, 64.0 / 33, 160.0 / 81 }),
                 { "harry partch" }, tuning, "11-limit, from Genesis of a Music (1949); a degree a key");
            put ("the well-tuned piano", "well-tuned piano", "just intonation",
                 ratios ({ 1, 567.0 / 512, 9.0 / 8, 147.0 / 128, 21.0 / 16, 1323.0 / 1024, 189.0 / 128, 1.5, 49.0 / 32, 7.0 / 4, 441.0 / 256, 63.0 / 32 }),
                 { "la monte young" }, tuning, "7-limit, on E flat in the work; a ratio a key, not in order of pitch");
            put ("the harp of new albion", "harp of new albion", "just intonation",
                 ratios ({ 1, 16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3, 64.0 / 45, 1.5, 8.0 / 5, 5.0 / 3, 16.0 / 9, 15.0 / 8 }),
                 { "terry riley" }, tuning, "5-limit, on C sharp in the work");
            put ("hexany (1 3 5 7)", "hexany 1 3 5 7", "just intonation", ratios ({ 1, 7.0 / 6, 5.0 / 4, 35.0 / 24, 5.0 / 3, 7.0 / 4 }),
                 { "erv wilson" }, both, "the combination-product set of 1 3 5 7 two at a time, on 1·3");
            {
                std::vector<double> eight, sixteen;
                for (int n = 8; n < 16; ++n)
                    eight.push_back (cents (n / 8.0));
                for (int n = 16; n < 32; ++n)
                    sixteen.push_back (cents (n / 16.0));
                const std::vector<std::string> spectral { "james tenney", "gérard grisey and tristan murail", "glenn branca", "georg friedrich haas", "ben johnston" };
                put ("harmonic series (partials 8 to 15)", "harmonic series 8-15", "harmonic series", std::move (eight), spectral, both, "the overtones of a fundamental three octaves down");
                put ("harmonic series (partials 16 to 31)", "harmonic series 16-31", "harmonic series", std::move (sixteen), spectral, tuning,
                     "the overtones of a fundamental four octaves down; a degree a key");
            }
            // Non-octave scales: Bohlen-Pierce divides the tritave (3/1), Carlos's alpha, beta and
            // gamma repeat a single step (Beauty in the Beast, 1986).
            {
                std::vector<double> c;
                for (int k = 0; k < 13; ++k)
                    c.push_back (k * cents (3.0) / 13);
                put ("bohlen-pierce (equal)", "bohlen-pierce equal", "non-octave", std::move (c), { "heinz bohlen", "john r. pierce" }, tuning,
                     "13 equal steps of the tritave (3/1)").period = cents (3.0);
                put ("bohlen-pierce (just)", "bohlen-pierce just", "non-octave",
                     ratios ({ 1, 27.0 / 25, 25.0 / 21, 9.0 / 7, 7.0 / 5, 75.0 / 49, 5.0 / 3, 9.0 / 5, 49.0 / 25, 15.0 / 7, 7.0 / 3, 63.0 / 25, 25.0 / 9 }),
                     { "heinz bohlen", "john r. pierce" }, tuning, "odd harmonics only, repeating at the tritave").period = cents (3.0);
            }
            for (auto [name, step] : { std::pair { "carlos alpha", 77.965 }, { "carlos beta", 63.833 }, { "carlos gamma", 35.099 } })
                put (name, name, "non-octave", { 0.0 }, { "wendy carlos" }, tuning, "a step of " + std::to_string (step).substr (0, 6) + " cents, no octave").period = step;

            //==================================================================
            // West Asia.
            // Zalzal's neutral third (wusṭā Zalzal, 27/22) in a diatonic of neutral steps.
            put ("wusṭā zalzal diatonic", "wusta zalzal diatonic", "historical tuning",
                 ratios ({ 1, 9.0 / 8, 27.0 / 22, 4.0 / 3, 1.5, 27.0 / 16, 81.0 / 44 }), { "zalzal" }, both, "the ʿūd's frets with the neutral third 27/22");
            // Ṣafī al-Dīn's seventeen notes to the octave (Kitāb al-adwār): Pythagorean, limma and comma steps.
            put ("ṣafī al-dīn's 17 notes", "safi al-din 17", "historical tuning", chain (11, 5), { "ṣafī al-dīn al-urmawī" }, tuning,
                 "a chain of fourths and fifths: steps of limma, limma, comma; a degree a key");
            // The Turkish theory of Arel, Ezgi and Uzdilek: 24 Pythagorean notes from 53 commas.
            put ("arel-ezgi-uzdilek 24", "arel-ezgi-uzdilek 24", "historical tuning", chain (12, 11), { "arel, ezgi and uzdilek", "rauf yekta", "ottoman empire (makam)" },
                 tuning, "24 notes of a chain of pure fifths, comma, limma and apotome steps; a degree a key");

            // Arabic maqāmāt besides those in Tunings.cpp, and the ajnās they are built from.
            const auto maqam = [&] (std::string name, std::string plain, std::vector<double> c, std::vector<std::string> from, std::string note = {})
            { put (name, plain, "maqam", std::move (c), std::move (from), melodic, std::move (note)); };
            maqam ("nikrīz", "nikriz", { 0, 200, 300, 600, 700, 900, 1000 }, { "arabic maqām" });
            maqam ("būsalīk", "busalik", { 0, 200, 300, 500, 700, 800, 1100 }, { "arabic maqām" }, "nahāwand's steps on D or G");
            maqam ("ʿirāq", "iraq", { 0, 150, 350, 500, 650, 850, 1050 }, { "arabic maqām", "iraq (maqām ʿirāqī)" }, "on B half-flat, rāst's notes");
            for (auto [name, c] : { std::pair { "jins rāst", std::vector<double> { 0, 200, 350, 500 } }, { "jins bayātī", { 0, 150, 300, 500 } },
                                    { "jins sīkāh", { 0, 150, 350 } }, { "jins ḥijāz", { 0, 100, 400, 500 } }, { "jins ṣabā", { 0, 150, 300, 400 } },
                                    { "jins nahāwand", { 0, 200, 300, 500, 700 } }, { "jins ʿajam", { 0, 200, 400 } },
                                    { "jins kurd", { 0, 100, 300, 500 } }, { "jins nikrīz", { 0, 200, 300, 600, 700 } } })
                put (name, ascii (name), "jins", c, { "arabic maqām", "rodolphe d'erlanger" }, melodic, "a jins: the tetrachord (or tri- or pentachord) a maqām is built from");

            // Turkish makams in the 53 commas of Arel-Ezgi-Uzdilek: a whole tone 9, the bakiye 4,
            // küçük mücennep 5, büyük mücennep 8, artık ikili 12.
            const auto makam = [&] (std::string name, std::string plain, std::initializer_list<double> commas, std::string note)
            { put (name, plain, "makam", units (commas, 53.0), { "ottoman empire (makam)", "dimitrie cantemir", "rauf yekta" }, melodic, std::move (note)); };
            makam ("rast (makam)", "rast (makam)", { 9, 8, 5, 9, 9, 8, 5 }, "rast tetrachord and pentachord");
            makam ("uşşak", "ussak", { 8, 5, 9, 9, 4, 9, 9 }, "uşşak tetrachord, buselik pentachord");
            makam ("hüseynî", "huseyni", { 8, 5, 9, 9, 8, 5, 9 }, "ascending-descending; muhayyer's notes");
            makam ("muhayyer", "muhayyer", { 8, 5, 9, 9, 8, 5, 9 }, "hüseynî's notes, descending from the octave");
            makam ("hicaz", "hicaz", { 5, 12, 5, 9, 8, 5, 9 }, "hicaz tetrachord, rast pentachord");
            makam ("kürdî", "kurdi", { 4, 9, 9, 9, 4, 9, 9 }, "kürdî tetrachord");
            makam ("nihâvend", "nihavend", { 9, 4, 9, 9, 4, 9, 9 }, "buselik pentachord, kürdî tetrachord");
            makam ("segâh (makam)", "segah (makam)", { 5, 9, 9, 8, 5, 9, 8 }, "rast's notes from segâh");
            makam ("ırak", "irak", { 5, 9, 8, 5, 9, 9, 8 }, "rast's notes from ırak");

            // Persian āvāz, read on their dastgāh's notes (šur's, homāyun's for bayāt-e eṣfahān).
            const auto avaz = [&] (std::string name, std::string plain, std::vector<double> c, std::string note)
            { put (name, plain, "avaz", std::move (c), { "iran (dastgāh)" }, melodic, std::move (note)); };
            avaz ("abuʿaṭā", "abu ata", { 0, 150, 300, 500, 700, 800, 1000 }, "of šur, its 4th the shāhed");
            avaz ("bayāt-e tork", "bayat-e tork", { 0, 200, 400, 500, 700, 900, 1050 }, "of šur, from its 3rd");
            avaz ("afšāri", "afshari", { 0, 200, 300, 500, 700, 850, 1000 }, "of šur, from its 4th (approximate)");
            avaz ("daštī", "dashti", { 0, 150, 300, 500, 700, 800, 1000 }, "of šur, its 5th the shāhed");
            avaz ("bayāt-e eṣfahān", "bayat-e esfahan", { 0, 200, 300, 500, 700, 850, 1100 }, "of homāyun, from its 4th");

            // Azerbaijani mugam, as Üzeyir Hacıbəyli's Principles of Azerbaijani Folk Music (1945)
            // builds their scales: perfect fourths on each degree of one tetrachord, the
            // tetrachords joined, so they repeat at the fourth. (His bayatı-şiraz, şüştər and
            // humayun join theirs in ways the English edition leaves unclear; not yet here.)
            const auto mugam = [&] (std::string name, std::string plain, std::initializer_list<int> steps, double period, std::string note)
            {
                put (name, plain, "mugam", semitones (steps), { "azerbaijan (mugam)", "üzeyir hacıbəyli" }, melodic, std::move (note)).period = period;
            };
            mugam ("rast (mugam)", "rast (mugam)", { 0, 2, 4 }, 500, "tetrachord 1 1 ½, joined");
            mugam ("şur", "shur (mugam)", { 0, 2, 3 }, 500, "tetrachord 1 ½ 1, joined");
            mugam ("segah (mugam)", "segah (mugam)", { 0, 1, 3 }, 500, "tetrachord ½ 1 1, joined");
            mugam ("çahargah", "chahargah (mugam)", { 0, 1, 4 }, 500, "tetrachord ½ 1½ ½, joined");

            //==================================================================
            // South Asia.
            // The 72 melakartas of Venkaṭamakhin (Caturdaṇḍīprakāśikā, 1660), in Govindācārya's
            // names: ma (śuddha, then prati) × six ri–ga × six dha–ni, in 12-TET.
            {
                static const char* const names[72] {
                    "kanakāṅgi", "ratnāṅgi", "gānamūrti", "vanaspati", "mānavati", "tānarūpi", "sēnāpati", "hanumatōḍi", "dhēnuka",
                    "nāṭakapriya", "kōkilapriya", "rūpavati", "gāyakapriya", "vakuḷābharaṇam", "māyāmāḷavagauḷa", "cakravākam",
                    "sūryakāntam", "hāṭakāmbari", "jhaṅkāradhvani", "naṭabhairavi", "kīravāṇi", "kharaharapriya", "gaurīmanōhari",
                    "varuṇapriya", "māraraṅjani", "cārukēśi", "sarasāṅgi", "harikāmbhōji", "dhīraśaṅkarābharaṇam", "nāganandini",
                    "yāgapriya", "rāgavardhini", "gāṅgēyabhūṣaṇi", "vāgadhīśvari", "śūlini", "calanāṭa", "sālagam", "jalārṇavam",
                    "jhālavarāḷi", "navanītam", "pāvani", "raghupriya", "gavāmbhōdi", "bhavapriya", "śubhapantuvarāḷi",
                    "ṣaḍvidhamārgiṇi", "suvarṇāṅgi", "divyamaṇi", "dhavaḷāmbari", "nāmanārāyaṇi", "kāmavardhini", "rāmapriya",
                    "gamanaśrama", "viśvambhari", "śyāmalāṅgi", "ṣaṇmukhapriya", "siṃhēndramadhyamam", "hēmavati", "dharmavati",
                    "nītimati", "kāntāmaṇi", "riṣabhapriya", "latāṅgi", "vācaspati", "mēcakalyāṇi", "citrāmbari", "sucaritra",
                    "jyōtisvarūpiṇi", "dhātuvardhani", "nāsikabhūṣaṇi", "kōsalam", "rasikapriya" };
                const int riga[6][2] { { 1, 2 }, { 1, 3 }, { 1, 4 }, { 2, 3 }, { 2, 4 }, { 3, 4 } };
                const int dhani[6][2] { { 8, 9 }, { 8, 10 }, { 8, 11 }, { 9, 10 }, { 9, 11 }, { 10, 11 } };
                for (int n = 1; n <= 72; ++n)
                {
                    const int i = (n - 1) % 36;
                    const auto& rg = riga[i / 6];
                    const auto& dn = dhani[i % 6];
                    const int ma = n <= 36 ? 5 : 6;
                    const auto number = std::to_string (n);
                    put (std::string (names[n - 1]) + " (" + number + ")", "melakarta " + number + " (" + ascii (names[n - 1]) + ")", "melakarta",
                         semitones ({ 0, rg[0], rg[1], ma, 7, dn[0], dn[1] }), { "karnatak (south india)", "venkaṭamakhin" }, melodic,
                         "melakarta " + number + ", chakra " + std::to_string ((n - 1) / 6 + 1) + "; in 12-TET");
                }
            }
            // The Nāṭyaśāstra: the 22 śrutis (in the ratios of the modern reading), its two grāmas
            // and the seven mūrcchanās of each, an octave species read from each note going down.
            {
                const auto sruti = ratios ({ 1, 256.0 / 243, 16.0 / 15, 10.0 / 9, 9.0 / 8, 32.0 / 27, 6.0 / 5, 5.0 / 4, 81.0 / 64, 4.0 / 3, 27.0 / 20,
                                             45.0 / 32, 729.0 / 512, 1.5, 128.0 / 81, 8.0 / 5, 5.0 / 3, 27.0 / 16, 16.0 / 9, 9.0 / 5, 15.0 / 8, 243.0 / 128 });
                put ("22 śruti", "22 shruti", "sruti", sruti, { "ancient india (nāṭyaśāstra)", "bharata" }, tuning, "a degree a key");
                // Sa, Ri, Ga, Ma, Pa, Dha, Ni on the 3rd, 5th, 9th, 13th, 16th and 18th śruti above Sa; the
                // madhyama-grāma's Pa a śruti lower (40/27).
                const auto sa = ratios ({ 1, 10.0 / 9, 32.0 / 27, 4.0 / 3, 1.5, 5.0 / 3, 16.0 / 9 });
                const auto ma = ratios ({ 1, 10.0 / 9, 32.0 / 27, 4.0 / 3, 40.0 / 27, 5.0 / 3, 16.0 / 9 });
                put ("ṣaḍja-grāma", "shadja-grama", "grama", sa, { "ancient india (nāṭyaśāstra)", "bharata" }, both, "Sa 4, Ri 3, Ga 2, Ma 4, Pa 4, Dha 3, Ni 2 śrutis");
                put ("madhyama-grāma", "madhyama-grama", "grama", ma, { "ancient india (nāṭyaśāstra)", "bharata" }, both, "Pa a śruti low");
                const struct { const char* name; int from; } sad[] { { "uttaramandrā", 0 }, { "rajanī", 6 }, { "uttarāyatā", 5 }, { "śuddhaṣaḍjā", 4 },
                                                                       { "matsarīkṛtā", 3 }, { "aśvakrāntā", 2 }, { "abhirudgatā", 1 } };
                const struct { const char* name; int from; } mad[] { { "sauvīrī", 3 }, { "hariṇāśvā", 2 }, { "kalopanatā", 1 }, { "śuddhamadhyā", 0 },
                                                                       { "mārgī", 6 }, { "pauravī", 5 }, { "hṛṣyakā", 4 } };
                static const char* const svara[7] { "sa", "ri", "ga", "ma", "pa", "dha", "ni" };
                for (const auto& m : sad)
                    put (std::string (m.name) + " (mūrcchanā)", ascii (m.name) + " (murchana)", "murchana", rotate (sa, (size_t) m.from),
                         { "ancient india (nāṭyaśāstra)", "bharata" }, melodic, std::string ("ṣaḍja-grāma from ") + svara[m.from]);
                for (const auto& m : mad)
                    put (std::string (m.name) + " (mūrcchanā)", ascii (m.name) + " (murchana)", "murchana", rotate (ma, (size_t) m.from),
                         { "ancient india (nāṭyaśāstra)", "bharata" }, melodic, std::string ("madhyama-grāma from ") + svara[m.from]);
            }

            //==================================================================
            // Southeast Asia.
            // Bali: the other patutan of the seven-tone saih pitu, its tones numbered 1 to 7 as the
            // research gives them, on the Javanese pélog of Tunings.cpp, each from its first tone.
            {
                const std::map<int, double> pelog { { 1, 0.0 }, { 2, 120.0 }, { 3, 258.0 }, { 4, 539.0 }, { 5, 675.0 }, { 6, 785.0 }, { 7, 943.0 } };
                const auto patutan = [&] (std::string name, std::initializer_list<int> use, std::string note)
                {
                    std::vector<double> c;
                    const auto from = pelog.at (*use.begin());
                    for (auto tone : use)
                    {
                        auto d = pelog.at (tone) - from;
                        c.push_back (d < 0 ? d + 1200.0 : d);
                    }
                    std::sort (c.begin(), c.end());
                    put (name, ascii (name), "balinese gamelan", std::move (c), { "bali" }, both, std::move (note));
                };
                patutan ("patutan baro", { 1, 3, 4, 5, 7 }, "tones 1 3 4 5 7; rare, loosely defined");
                patutan ("patutan lebeng", { 1, 2, 3, 4, 5, 6, 7 }, "all seven (semar pagulingan); five in gambuh");
                patutan ("sléndro gedé", { 2, 3, 4, 6, 7 }, "tones 2 3 4 6 7; gendèr wayang");
                patutan ("sléndro alit", { 3, 4, 5, 7 }, "tones (1) 3 4 5 7; the four-tone angklung");
                patutan ("pengenter gedé", { 1, 2, 4, 5, 7 }, "tones 1 2 4 5 7; recent compositions");
                patutan ("pengenter alit", { 1, 3, 4, 6, 7 }, "tones 1 3 4 6 7; recent compositions");
            }
            // Thailand, Cambodia and Laos: Morton's five pentatonic modes (1976) on the 1 2 3 5 6 of
            // the seven near-equal steps (seven equal steps here, about 171 cents).
            for (auto [mode, start] : { std::pair { 1, 0 }, { 2, 1 }, { 3, 2 }, { 5, 3 }, { 6, 4 } })
            {
                const int core[5] { 0, 1, 2, 4, 5 };
                std::vector<double> c;
                for (int i = 0; i < 5; ++i)
                    c.push_back (((core[(start + i) % 5] - core[start] + 7) % 7) * 1200.0 / 7);
                const auto name = "thai mode " + std::to_string (mode) + " (morton)";
                put (name, name, "thai mode", std::move (c), { "thailand", "cambodia (khmer)", "laos" }, both,
                     mode == 1 ? "the most used" : mode == 3 || mode == 5 ? "rare" : "common");
            }
            // Vietnam: hò xự xang xê cống as measured (Nhan, 2017), and điệu nam's hơi ai with xư.
            put ("điệu bắc", "dieu bac", "vietnamese mode", { 0, 171, 514, 685, 887 }, { "vietnam" }, both, "hò xự xang xê cống, measured; bright, joyful");
            put ("điệu nam (hơi ai)", "dieu nam (hoi ai)", "vietnamese mode", { 0, 342, 514, 685, 887 }, { "vietnam" }, both, "hò xư xang xê cống; the mourning air");

            //==================================================================
            // East Asia: the koto's kumoi-jōshi (the others are in Tunings.cpp).
            put ("kumoijōshi", "kumoijoshi", "japanese mode", semitones ({ 0, 2, 3, 7, 9 }), { "japan" }, melodic, "a koto tuning, as a scale");

            //==================================================================
            // Africa: the Ethiopian qenet, the four pentatonic modes of the krar and the masenqo.
            const auto qenet = [&] (std::string name, std::string plain, std::initializer_list<int> steps)
            { put (name, plain, "qenet", semitones (steps), { "ethiopia" }, melodic); };
            qenet ("təzəta (major)", "tezeta major", { 0, 2, 4, 7, 9 });
            qenet ("təzəta (minor)", "tezeta minor", { 0, 2, 3, 7, 8 });
            qenet ("bati (major)", "bati major", { 0, 4, 5, 7, 11 });
            qenet ("bati (minor)", "bati minor", { 0, 3, 5, 7, 10 });
            qenet ("ambasäl", "ambassel", { 0, 1, 5, 7, 8 });
            qenet ("anči hoye", "anchihoye", { 0, 1, 5, 6, 9 });
        }

        //======================================================================
        // Where each system of the list is from: the families' own, and the places the research
        // names for systems already in the list (a mode under each composer who made it theirs).
        void place (std::vector<TuningSystem>& all)
        {
            static const std::map<std::string, std::pair<std::vector<std::string>, unsigned>> families {
                { "raga", { { "hindustani (north india)" }, melodic } },
                { "church mode", { { "western church (gregorian chant)" }, melodic } },
                { "hypo-mode", { { "western church (gregorian chant)" }, melodic } },
                { "jazz mode", { { "jazz (united states)" }, melodic } },
                { "messiaen mode", { { "olivier messiaen" }, melodic } },
                { "maqam", { { "arabic maqām" }, melodic } },
                { "dastgah", { { "iran (dastgāh)" }, melodic } },
                { "javanese gamelan", { { "central java" }, melodic | tuning } },
                { "balinese gamelan", { { "bali" }, melodic | tuning } },
                { "sundanese gamelan", { { "sunda (west java)" }, melodic | tuning } },
                { "mbira dzavadzimu", { { "zimbabwe (shona)" }, melodic | tuning } },
                { "nyunga nyunga", { { "zimbabwe (shona)" }, melodic | tuning } },
                { "matepe", { { "korekore and sena (matepe)" }, melodic | tuning } },
                { "chinese tuning", { { "china" }, tuning } },
                { "chinese mode", { { "china" }, melodic } },
                { "chinese regional mode", { { "china" }, melodic } },
                { "gagaku mode", { { "japan" }, melodic } },
                { "japanese mode", { { "japan" }, melodic } },
                { "korean tuning", { { "korea" }, tuning } },
                { "korean mode", { { "korea" }, melodic } },
                { "korean folk mode", { { "korea" }, melodic } },
                { "equal", { {}, tuning } },
            };
            for (auto& t : all)
                if (t.kinds == 0)
                    if (auto it = families.find (t.family); it != families.end())
                    {
                        t.from = it->second.first;
                        t.kinds = it->second.second;
                    }

            const std::vector<std::pair<std::string, std::vector<std::string>>> more {
                // Europe
                { "heinrich glarean", { "aeolian", "hypoaeolian", "ionian", "hypoionian" } },
                { "gioseffo zarlino", { "dorian", "hypodorian", "phrygian", "hypophrygian", "lydian", "hypolydian", "mixolydian",
                                        "hypomixolydian", "aeolian", "hypoaeolian", "ionian", "hypoionian" } },
                { "claude le jeune", { "dorian", "hypodorian", "phrygian", "hypophrygian", "lydian", "hypolydian", "mixolydian",
                                       "hypomixolydian", "aeolian", "hypoaeolian", "ionian", "hypoionian" } },
                { "johannes lippius and johann crüger", { "ionian", "lydian", "mixolydian", "dorian", "phrygian", "aeolian" } },
                { "tonal western music", { "ionian", "aeolian", "melodic minor" } },
                { "ludwig van beethoven", { "lydian" } },
                { "frédéric chopin", { "lydian" } },
                { "mikhail glinka", { "whole tone", "messiaen mode 1" } },
                { "modest musorgsky", { "dorian", "aeolian" } },
                { "nikolai rimsky-korsakov", { "messiaen mode 2", "diminished half-whole", "whole tone" } },
                { "gustav mahler", { "lydian" } },
                { "leoš janáček", { "lydian", "mixolydian", "dorian" } },
                { "gabriel fauré", { "lydian dominant" } },
                { "claude debussy", { "whole tone", "messiaen mode 1", "dorian", "lydian dominant" } },
                { "maurice ravel", { "lydian dominant" } },
                { "nikolai and alexander tcherepnin", { "messiaen mode 3" } },
                { "béla bartók", { "lydian dominant", "dorian", "phrygian", "mixolydian", "aeolian" } },
                { "igor stravinsky", { "messiaen mode 2", "diminished half-whole" } },
                { "ralph vaughan williams and gustav holst", { "dorian", "mixolydian", "aeolian" } },
                { "british and irish folk", { "ionian", "mixolydian", "dorian", "aeolian" } },
                { "eastern european folk", { "dorian", "phrygian", "mixolydian", "aeolian", "lydian" } },
                { "spain (andalusia, flamenco)", { "phrygian" } },
                { "ferruccio busoni", { "36 equal steps" } },
                { "christiaan huygens", { "31 equal steps" } },
                { "nicola vicentino", { "31 equal steps" } },
                { "adriaan fokker", { "31 equal steps" } },
                { "alois hába", { "24 equal steps", "18 equal steps", "36 equal steps", "72 equal steps" } },
                { "ivan wyschnegradsky", { "24 equal steps" } },
                { "byzantine and greek orthodox chant", { "72 equal steps" } },
                // West Asia
                { "mīkhāʾīl mushāqa", { "24 equal steps" } },
                { "arel, ezgi and uzdilek", { "53 equal steps" } },
                { "ottoman empire (makam)", { "53 equal steps" } },
                { "rodolphe d'erlanger", { "rast", "bayati", "sikah", "hijaz", "saba", "nahawand", "ajam", "kurd", "huzam" } },
                { "iraq (maqām ʿirāqī)", { "rast", "bayati", "hijaz", "saba" } },
                // South Asia
                { "v. n. bhatkhande", { "bilaval", "yaman (kalyan)", "khamaj", "kafi", "asavari", "bhairavi", "bhairav", "purvi", "marva", "todi" } },
                { "hanumat mata", { "bhairav", "malkauns", "dipak", "shri", "megh" } },
                // Southeast Asia
                { "martopangrawit", { "slendro pathet nem", "slendro pathet sanga", "slendro pathet manyura", "pelog pathet lima", "pelog pathet nem", "pelog pathet barang" } },
                { "thailand", { "7 equal steps" } },
                { "cambodia (khmer)", { "7 equal steps" } },
                { "laos", { "7 equal steps" } },
                // East Asia
                { "jing fang", { "jing fang 60 lu" } },
                { "he chengtian", { "he chengtian xinlu" } },
                { "cai yuanding", { "cai yuanding 18 lu" } },
                { "zhu zaiyu", { "zhu zaiyu xinfa milu", "12 equal steps" } },
                { "yang yinliu", { "yayue gong", "qingyue gong", "yanyue gong" } },
                { "yatsuhashi kengyō", { "hirajoshi", "kumoijoshi" } },
                { "uehara rokushirō", { "miyako-bushi (in)", "in (ascending)", "ritsu (yo)" } },
                { "koizumi fumio", { "min'yo", "ryukyu", "miyako-bushi (in)", "ritsu (yo)" } },
                { "okinawa (ryūkyū kingdom)", { "ryukyu" } },
                // The Americas
                { "george russell", { "lydian", "lydian augmented", "lydian dominant", "whole tone", "diminished whole-half", "diminished half-whole", "blues" } },
                { "roy harris", { "lydian", "ionian", "mixolydian", "dorian", "aeolian", "phrygian" } },
                { "henry cowell", { "dorian", "mixolydian", "aeolian" } },
                { "charles ives", { "24 equal steps" } },
                { "julián carrillo", { "24 equal steps", "48 equal steps", "96 equal steps" } },
                { "joseph yasser", { "19 equal steps" } },
                { "joe maneri and ezra sims", { "72 equal steps" } },
                { "lou harrison", { "intense diatonic (ptolemy)", "zarlino just intonation", "hexany 1 3 5 7" } },
                { "ben johnston", { "zarlino just intonation" } },
                { "easley blackwood", { "13 equal steps", "14 equal steps", "15 equal steps", "16 equal steps", "17 equal steps", "18 equal steps",
                                        "19 equal steps", "20 equal steps", "21 equal steps", "22 equal steps", "23 equal steps", "24 equal steps" } },
                { "ivor darreg", { "13 equal steps", "17 equal steps", "19 equal steps", "22 equal steps", "31 equal steps" } },
                { "southern appalachia", { "ionian", "mixolydian", "dorian", "aeolian" } },
            };
            for (const auto& [origin, plains] : more)
                for (const auto& plain : plains)
                    if (const auto i = findTuningIn (all, plain); i >= 0)
                    {
                        auto& from = all[(size_t) i].from;
                        if (std::find (from.begin(), from.end(), origin) == from.end())
                            from.push_back (origin);
                    }
        }
    }

    //==========================================================================
    const std::vector<std::string>& regions()
    {
        static const std::vector<std::string> all { "europe", "west asia", "central asia", "south asia", "southeast asia",
                                                    "east asia", "africa", "the americas", "oceania" };
        return all;
    }

    std::string regionTitle (const std::string& region)
    {
        static const std::vector<std::pair<std::string, std::string>> titles {
            { "europe", "Europe" }, { "west asia", "West Asia" }, { "central asia", "Central Asia" }, { "south asia", "South Asia" },
            { "southeast asia", "Southeast Asia" }, { "east asia", "East Asia" }, { "africa", "Africa" },
            { "the americas", "the Americas" }, { "oceania", "Oceania" } };
        for (const auto& [name, title] : titles)
            if (name == region)
                return title;
        return region;
    }

    const std::vector<Origin>& origins()
    {
        static const std::vector<Origin> all = []
        {
            std::vector<Origin> o {
                // Europe: traditions
                { "ancient greece", "europe", -600, "", false, "Ancient Greece" },
                { "byzantine and greek orthodox chant", "europe", 700, "", false, "Byzantine and Greek Orthodox chant" },
                { "western church (gregorian chant)", "europe", 800, "", false, "Western Church (Gregorian chant)" },
                { "russian orthodox (znamenny chant)", "europe", 1100, "", false, "Russian Orthodox (znamenny chant)" },
                { "ashkenazi jewish liturgy (nusach)", "europe", 1500, "", false, "Ashkenazi Jewish liturgy (nusach)" },
                { "tonal western music", "europe", 1700, "from c. 1700", false, "tonal Western music" },
                { "british and irish folk", "europe", 1700, "", false, "British and Irish folk" },
                { "eastern european folk", "europe", 1700, "", false, "Eastern European folk" },
                { "hungary", "europe", 1800, "romani and peasant music", false, "Hungary" },
                { "spain (andalusia, flamenco)", "europe", 1800, "", false, "Spain (Andalusia, flamenco)" },
                // Europe: people
                { "pythagoras (school of)", "europe", -570, "6th c. bce", true, "Pythagoras (school of)" },
                { "archytas", "europe", -428, "c. 428–347 bce", true, "Archytas" },
                { "aristoxenus", "europe", -375, "4th c. bce", true, "Aristoxenus" },
                { "ptolemy", "europe", 100, "c. 100–170", true, "Ptolemy" },
                { "guido of arezzo", "europe", 991, "c. 991–1033", true, "Guido of Arezzo" },
                { "bartolomé ramos de pareja", "europe", 1440, "c. 1440–1522", true, "Bartolomé Ramos de Pareja" },
                { "heinrich glarean", "europe", 1488, "1488–1563", true, "Heinrich Glarean" },
                { "nicola vicentino", "europe", 1511, "1511–1576", true, "Nicola Vicentino" },
                { "gioseffo zarlino", "europe", 1517, "1517–1590", true, "Gioseffo Zarlino" },
                { "claude le jeune", "europe", 1530, "c. 1530–1600", true, "Claude Le Jeune" },
                { "johannes lippius and johann crüger", "europe", 1585, "1585–1612; 1598–1662", true, "Johannes Lippius and Johann Crüger" },
                { "christiaan huygens", "europe", 1629, "1629–1695", true, "Christiaan Huygens" },
                { "andreas werckmeister", "europe", 1645, "1645–1706", true, "Andreas Werckmeister" },
                { "johann philipp kirnberger", "europe", 1721, "1721–1783", true, "Johann Philipp Kirnberger" },
                { "ludwig van beethoven", "europe", 1770, "1770–1827", true, "Ludwig van Beethoven" },
                { "mikhail glinka", "europe", 1804, "1804–1857", true, "Mikhail Glinka" },
                { "frédéric chopin", "europe", 1810, "1810–1849", true, "Frédéric Chopin" },
                { "franz liszt", "europe", 1811, "1811–1886", true, "Franz Liszt" },
                { "giuseppe verdi", "europe", 1813, "1813–1901", true, "Giuseppe Verdi" },
                { "camille saint-saëns", "europe", 1835, "1835–1921", true, "Camille Saint-Saëns" },
                { "modest musorgsky", "europe", 1839, "1839–1881", true, "Modest Musorgsky" },
                { "nikolai rimsky-korsakov", "europe", 1844, "1844–1908", true, "Nikolai Rimsky-Korsakov" },
                { "gabriel fauré", "europe", 1845, "1845–1924", true, "Gabriel Fauré" },
                { "leoš janáček", "europe", 1854, "1854–1928", true, "Leoš Janáček" },
                { "cecil sharp", "europe", 1859, "1859–1924", true, "Cecil Sharp" },
                { "gustav mahler", "europe", 1860, "1860–1911", true, "Gustav Mahler" },
                { "claude debussy", "europe", 1862, "1862–1918", true, "Claude Debussy" },
                { "annie gilchrist", "europe", 1863, "1863–1954", true, "Annie Gilchrist" },
                { "ferruccio busoni", "europe", 1866, "1866–1924", true, "Ferruccio Busoni" },
                { "alexander scriabin", "europe", 1872, "1872–1915", true, "Alexander Scriabin" },
                { "ralph vaughan williams and gustav holst", "europe", 1872, "1872–1958; 1874–1934", true, "Ralph Vaughan Williams and Gustav Holst" },
                { "nikolai and alexander tcherepnin", "europe", 1873, "1873–1945; 1899–1977", true, "Nikolai and Alexander Tcherepnin" },
                { "maurice ravel", "europe", 1875, "1875–1937", true, "Maurice Ravel" },
                { "béla bartók", "europe", 1881, "1881–1945", true, "Béla Bartók" },
                { "zoltán kodály", "europe", 1882, "1882–1967", true, "Zoltán Kodály" },
                { "igor stravinsky", "europe", 1882, "1882–1971", true, "Igor Stravinsky" },
                { "abraham zvi idelsohn", "europe", 1882, "1882–1938", true, "Abraham Zvi Idelsohn" },
                { "adriaan fokker", "europe", 1887, "1887–1972", true, "Adriaan Fokker" },
                { "alois hába", "europe", 1893, "1893–1973", true, "Alois Hába" },
                { "ivan wyschnegradsky", "europe", 1893, "1893–1979", true, "Ivan Wyschnegradsky" },
                { "olivier messiaen", "europe", 1908, "1908–1992", true, "Olivier Messiaen" },
                { "heinz bohlen", "europe", 1935, "1935–2016", true, "Heinz Bohlen" },
                { "gérard grisey and tristan murail", "europe", 1946, "1946–1998; b. 1947", true, "Gérard Grisey and Tristan Murail" },
                { "georg friedrich haas", "europe", 1953, "b. 1953", true, "Georg Friedrich Haas" },
                // West Asia
                { "arabic maqām", "west asia", 700, "", false, "Arabic maqām" },
                { "iran (dastgāh)", "west asia", 800, "the radif, codified in the 19th c.", false, "Iran (dastgāh)" },
                { "ottoman empire (makam)", "west asia", 1300, "and modern turkey", false, "Ottoman Empire (makam)" },
                { "iraq (maqām ʿirāqī)", "west asia", 1500, "", false, "Iraq (maqām ʿirāqī)" },
                { "azerbaijan (mugam)", "west asia", 1500, "", false, "Azerbaijan (mugam)" },
                { "zalzal", "west asia", 750, "8th c.", true, "Zalzal" },
                { "ṣafī al-dīn al-urmawī", "west asia", 1216, "d. 1294", true, "Ṣafī al-Dīn al-Urmawī" },
                { "dimitrie cantemir", "west asia", 1673, "1673–1723", true, "Dimitrie Cantemir" },
                { "mīkhāʾīl mushāqa", "west asia", 1800, "1800–1888", true, "Mīkhāʾīl Mushāqa" },
                { "rauf yekta", "west asia", 1871, "1871–1935", true, "Rauf Yekta" },
                { "rodolphe d'erlanger", "west asia", 1872, "1872–1932", true, "Rodolphe d'Erlanger" },
                { "arel, ezgi and uzdilek", "west asia", 1880, "early 20th c.", true, "Arel, Ezgi and Uzdilek" },
                { "üzeyir hacıbəyli", "west asia", 1885, "1885–1948", true, "Üzeyir Hacıbəyli" },
                // South Asia
                { "ancient india (nāṭyaśāstra)", "south asia", -200, "", false, "ancient India (Nāṭyaśāstra)" },
                { "hindustani (north india)", "south asia", 1300, "", false, "Hindustani (North India)" },
                { "karnatak (south india)", "south asia", 1400, "", false, "Karnatak (South India)" },
                { "hanumat mata", "south asia", 1500, "six rāgas and their rāginīs", false, "Hanumat mata" },
                { "bharata", "south asia", -200, "nāṭyaśāstra, c. 200 bce–200 ce", true, "Bharata" },
                { "venkaṭamakhin", "south asia", 1620, "17th c.", true, "Venkaṭamakhin" },
                { "v. n. bhatkhande", "south asia", 1860, "1860–1936", true, "V. N. Bhatkhande" },
                // Southeast Asia
                { "central java", "southeast asia", 1600, "", false, "Central Java" },
                { "bali", "southeast asia", 1600, "", false, "Bali" },
                { "sunda (west java)", "southeast asia", 1600, "", false, "Sunda (West Java)" },
                { "thailand", "southeast asia", 1350, "", false, "Thailand" },
                { "cambodia (khmer)", "southeast asia", 1350, "", false, "Cambodia (Khmer)" },
                { "laos", "southeast asia", 1350, "", false, "Laos" },
                { "vietnam", "southeast asia", 1000, "", false, "Vietnam" },
                { "martopangrawit", "southeast asia", 1914, "1914–1986", true, "Martopangrawit" },
                // East Asia
                { "china", "east asia", -1000, "", false, "China" },
                { "korea", "east asia", 1400, "", false, "Korea" },
                { "japan", "east asia", 700, "", false, "Japan" },
                { "okinawa (ryūkyū kingdom)", "east asia", 1429, "", false, "Okinawa (Ryūkyū Kingdom)" },
                { "jing fang", "east asia", -78, "78–37 bce", true, "Jing Fang" },
                { "he chengtian", "east asia", 370, "370–447", true, "He Chengtian" },
                { "cai yuanding", "east asia", 1135, "1135–1198", true, "Cai Yuanding" },
                { "zhu zaiyu", "east asia", 1536, "1536–1611", true, "Zhu Zaiyu" },
                { "yatsuhashi kengyō", "east asia", 1614, "1614–1685", true, "Yatsuhashi Kengyō" },
                { "uehara rokushirō", "east asia", 1848, "1848–1913", true, "Uehara Rokushirō" },
                { "yang yinliu", "east asia", 1899, "1899–1984", true, "Yang Yinliu" },
                { "koizumi fumio", "east asia", 1927, "1927–1983", true, "Koizumi Fumio" },
                // Africa
                { "ethiopia", "africa", 500, "", false, "Ethiopia" },
                { "zimbabwe (shona)", "africa", 1000, "", false, "Zimbabwe (Shona)" },
                { "korekore and sena (matepe)", "africa", 1000, "", false, "Korekore and Sena (matepe)" },
                // The Americas
                { "southern appalachia", "the americas", 1700, "", false, "Southern Appalachia" },
                { "jazz (united states)", "the americas", 1900, "", false, "jazz (United States)" },
                { "charles ives", "the americas", 1874, "1874–1954", true, "Charles Ives" },
                { "julián carrillo", "the americas", 1875, "1875–1965", true, "Julián Carrillo" },
                { "joseph yasser", "the americas", 1893, "1893–1981", true, "Joseph Yasser" },
                { "henry cowell", "the americas", 1897, "1897–1965", true, "Henry Cowell" },
                { "roy harris", "the americas", 1898, "1898–1979", true, "Roy Harris" },
                { "harry partch", "the americas", 1901, "1901–1974", true, "Harry Partch" },
                { "john r. pierce", "the americas", 1910, "1910–2002", true, "John R. Pierce" },
                { "lou harrison", "the americas", 1917, "1917–2003", true, "Lou Harrison" },
                { "ivor darreg", "the americas", 1917, "1917–1994", true, "Ivor Darreg" },
                { "george russell", "the americas", 1923, "1923–2009", true, "George Russell" },
                { "ben johnston", "the americas", 1926, "1926–2019", true, "Ben Johnston" },
                { "joe maneri and ezra sims", "the americas", 1927, "1927–2009; 1928–2015", true, "Joe Maneri and Ezra Sims" },
                { "erv wilson", "the americas", 1928, "1928–2016", true, "Erv Wilson" },
                { "easley blackwood", "the americas", 1933, "1933–2023", true, "Easley Blackwood" },
                { "james tenney", "the americas", 1934, "1934–2006", true, "James Tenney" },
                { "la monte young", "the americas", 1935, "b. 1935", true, "La Monte Young" },
                { "terry riley", "the americas", 1935, "b. 1935", true, "Terry Riley" },
                { "wendy carlos", "the americas", 1939, "b. 1939", true, "Wendy Carlos" },
                { "glenn branca", "the americas", 1948, "1948–2018", true, "Glenn Branca" },
                // Oceania
                { "percy grainger", "oceania", 1882, "1882–1961, born in melbourne", true, "Percy Grainger" },
            };
            // Traditions before people, then by date.
            std::stable_sort (o.begin(), o.end(), [] (const Origin& a, const Origin& b)
            {
                const auto ra = std::find (regions().begin(), regions().end(), a.region) - regions().begin();
                const auto rb = std::find (regions().begin(), regions().end(), b.region) - regions().begin();
                return std::tuple (ra, a.person, a.year) < std::tuple (rb, b.person, b.year);
            });
            return o;
        }();
        return all;
    }

    const Origin* findOrigin (const std::string& name)
    {
        for (const auto& o : origins())
            if (o.name == name)
                return &o;
        return nullptr;
    }

    std::vector<int> systemsFrom (const std::string& origin, unsigned kind)
    {
        std::vector<int> out;
        const auto& all = tuningSystems();
        for (size_t i = 0; i < all.size(); ++i)
            if ((all[i].kinds & kind) != 0 && std::find (all[i].from.begin(), all[i].from.end(), origin) != all[i].from.end())
                out.push_back ((int) i);
        return out;
    }

    std::vector<double> octaveCents (const TuningSystem& t)
    {
        if (t.period >= 1200.0 - 1.0e-6 && t.period <= 1200.0 + 1.0e-6)
            return t.cents;
        std::vector<double> out;
        for (int k = 0; k * t.period < 1200.0 - 1.0e-6 && k < 1200; ++k)
            for (auto c : t.cents)
                if (const auto d = k * t.period + c; d < 1200.0 - 1.0e-6)
                    out.push_back (d);
        std::sort (out.begin(), out.end());
        out.erase (std::unique (out.begin(), out.end(), [] (double a, double b) { return std::abs (a - b) < 1.0e-6; }), out.end());
        return out;
    }
}
