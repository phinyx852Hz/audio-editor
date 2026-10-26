// SPDX-License-Identifier: AGPL-3.0-only
#include "Rhythms.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace rhythm
{
    namespace
    {
        // General MIDI drums (channel 10).
        namespace gm
        {
            constexpr int kick = 36, sideStick = 37, snare = 38, clap = 39, floorTom = 41, closedHat = 42,
                          pedalHat = 44, lowTom = 45, openHat = 46, crash = 49, ride = 51, rideBell = 53,
                          tambourine = 54, cowbell = 56, hiBongo = 60, lowBongo = 61, muteHiConga = 62,
                          openHiConga = 63, lowConga = 64, highTimbale = 65, lowTimbale = 66, highAgogo = 67,
                          lowAgogo = 68, cabasa = 69, maracas = 70, shortGuiro = 73, longGuiro = 74, claves = 75,
                          hiWoodBlock = 76, lowWoodBlock = 77, muteTriangle = 80, openTriangle = 81;
        }

        std::string rep (const std::string& s, int n)
        {
            std::string out;
            for (int i = 0; i < n; ++i)
                out += s;
            return out;
        }

        Lane lane (std::string name, int note, int perBeat, std::string hits)
        {
            return { std::move (name), note, perBeat, std::move (hits), 9 };
        }

        //======================================================================
        // North Indian tāls, from their ṭhekā: the bols of each mātrā (several in one mātrā
        // joined by -, – a silent one), a vibhāg after each |, and the vibhāgs' signs: x sam,
        // 0 khālī, a number a tālī. The bols are sorted onto the tabla's strokes.
        enum Stroke { bayanOpen, bayanClosed, dayanOpen, dayanClosed, numStrokes };

        std::vector<Stroke> strokesOf (const std::string& bol)
        {
            for (const char* both : { "dhā", "dhin", "dhi", "dhī", "dha", "din", "dhe" })
                if (bol == both)
                    return { bayanOpen, dayanOpen };
            for (const char* b : { "ge", "ghe", "ga", "gi", "ghi" })
                if (bol == b)
                    return { bayanOpen };
            for (const char* b : { "ka", "kat", "ke", "ki", "kā" })
                if (bol == b)
                    return { bayanClosed };
            for (const char* d : { "ti", "ṭa", "ra", "ṭe", "te", "ṭi", "di" })
                if (bol == d)
                    return { dayanClosed };
            return { dayanOpen };   // tā, nā, na, tin, tī, tū, ta...
        }

        std::vector<std::string> split (const std::string& s, char by)
        {
            std::vector<std::string> out;
            size_t from = 0;
            while (from <= s.size())
            {
                const auto to = std::min (s.find (by, from), s.size());
                if (to > from)
                    out.push_back (s.substr (from, to - from));
                from = to + 1;
            }
            return out;
        }

        Rhythm tal (std::string name, std::string plain, std::string signs, std::string theka, double bpm, std::string about)
        {
            Rhythm r;
            r.name = std::move (name);
            r.plain = std::move (plain);
            r.group = "north indian tāl";
            r.bpm = bpm;
            r.about = std::move (about);

            std::vector<std::vector<std::string>> matras;   // each mātrā's bols
            for (const auto& vibhag : split (theka, '|'))
            {
                int count = 0;
                for (const auto& m : split (vibhag, ' '))
                {
                    matras.push_back (split (m, '-'));
                    ++count;
                }
                r.sections.push_back (count);
            }
            r.beats = (int) matras.size();
            int perBeat = 1;
            for (const auto& m : matras)
                perBeat = std::lcm (perBeat, (int) m.size());

            const auto length = (size_t) (r.beats * perBeat);
            std::string strokes[numStrokes];
            for (auto& s : strokes)
                s.assign (length, '.');
            for (size_t i = 0; i < matras.size(); ++i)
                for (size_t j = 0; j < matras[i].size(); ++j)
                {
                    const auto& bol = matras[i][j];
                    if (bol == "–")
                        continue;
                    const auto step = i * (size_t) perBeat + j * (size_t) perBeat / matras[i].size();
                    for (auto s : strokesOf (bol))
                        strokes[s][step] = i == 0 && j == 0 ? 'X' : 'x';
                }

            // The hands: a clap on sam and each tālī, a wave on each khālī.
            const auto label = split (signs, ' ');
            std::string claps (length, '.'), waves (length, '.');
            int start = 0;
            r.counting.clear();
            for (size_t v = 0; v < r.sections.size(); ++v)
            {
                const auto khali = v < label.size() && label[v] == "0";
                (khali ? waves : claps)[(size_t) (start * perBeat)] = v == 0 ? 'X' : 'x';
                if (v > 0)
                    r.counting += " | ";
                r.counting += (v < label.size() ? label[v] : std::string ("?")) + " ";
                for (int k = 0; k < r.sections[v]; ++k)
                {
                    const auto& m = matras[(size_t) (start + k)];
                    for (size_t j = 0; j < m.size(); ++j)
                        r.counting += m[j];
                    if (k + 1 < r.sections[v])
                        r.counting += " ";
                }
                start += r.sections[v];
            }

            const char* names[numStrokes] { "bāyāṃ, open (ge, dhā)", "bāyāṃ, closed (ka, kat)",
                                            "dāyāṃ, open (nā, tin, dhā)", "dāyāṃ, closed (ti, ra, ṭa)" };
            const int notes[numStrokes] { gm::lowConga, gm::lowBongo, gm::openHiConga, gm::muteHiConga };
            for (int s = 0; s < numStrokes; ++s)
                if (strokes[s].find_first_not_of ('.') != std::string::npos)
                    r.lanes.push_back (lane (names[s], notes[s], perBeat, strokes[s]));
            r.lanes.push_back (lane ("tālī (clap)", gm::clap, perBeat, claps));
            if (waves.find_first_not_of ('.') != std::string::npos)
                r.lanes.push_back (lane ("khālī (wave)", gm::sideStick, perBeat, waves));
            return r;
        }

        void addTals (std::vector<Rhythm>& all)
        {
            all.push_back (tal ("tīntāl", "tintal", "x 2 0 3",
                                "dhā dhin dhin dhā | dhā dhin dhin dhā | dhā tin tin tā | tā dhin dhin dhā", 120,
                                "16 mātrās, 4+4+4+4: the commonest tāl of Hindustani music, from vilambit to drut."));
            all.push_back (tal ("jhaptāl", "jhaptal", "x 2 0 3",
                                "dhī nā | dhī dhī nā | tī nā | dhī dhī nā", 120,
                                "10 mātrās, 2+3+2+3."));
            all.push_back (tal ("ektāl", "ektal", "x 0 2 0 3 4",
                                "dhin dhin | dhā-ge ti-ra-ki-ṭa | tū nā | kat tā | dhā-ge ti-ra-ki-ṭa | dhī nā", 60,
                                "12 mātrās in six vibhāgs of two; khayāl's slow tāl, played fast too."));
            all.push_back (tal ("rūpak", "rupak", "0 1 2",
                                "tī tī nā | dhī nā | dhī nā", 120,
                                "7 mātrās, 3+2+2. Its sam falls on the khālī, so the cycle begins with a wave."));
            all.push_back (tal ("dādrā", "dadra", "x 0",
                                "dhā dhī nā | dhā tī nā", 160,
                                "6 mātrās, 3+3: light classical and film songs."));
            all.push_back (tal ("kaharvā", "kaharva", "x 0",
                                "dhā ge na ti | na ka dhi na", 160,
                                "8 mātrās, 4+4: light classical, folk and film songs."));
            all.push_back (tal ("jhūmrā", "jhumra", "x 2 0 3",
                                "dhin –-dhā ti-ra-ki-ṭa | dhin dhin dhā-ge ti-ra-ki-ṭa | tin –-tā ti-ra-ki-ṭa | dhin dhin dhā-ge ti-ra-ki-ṭa", 50,
                                "14 mātrās, 3+4+3+4: vilambit khayāl."));
            all.push_back (tal ("dhamār", "dhamar", "x 2 0 3",
                                "ka dhi ṭa dhi ṭa | dhā – | ga ti ṭa | ti ṭa tā –", 90,
                                "14 mātrās, 5+2+3+4: the pakhāvaj tāl of dhamār (hori) songs."));
            all.push_back (tal ("cautāl", "chautal", "x 0 2 0 3 4",
                                "dhā dhā | din tā | ki-ṭa dhā | din tā | ti-ṭa ka-ta | ga-di ga-na", 100,
                                "12 mātrās in six vibhāgs of two: the pakhāvaj tāl of dhrupad."));
            all.push_back (tal ("āḍā cautāl", "ada chautal", "x 2 0 3 0 4 0",
                                "dhin ti-ra-ki-ṭa | dhī nā | tū nā | kat tā | ti-ra-ki-ṭa dhī | nā dhī | dhī nā", 80,
                                "14 mātrās in seven vibhāgs of two."));
            all.push_back (tal ("dīpcandī", "dipchandi", "x 2 0 3",
                                "dhā dhin – | dhā dhā tin – | tā tin – | dhā dhā dhin –", 90,
                                "14 mātrās, 3+4+3+4: ṭhumrī and hori."));
            all.push_back (tal ("tilvāḍā", "tilwada", "x 2 0 3",
                                "dhā ti-ra-ki-ṭa dhin dhin | dhā dhā tin tin | tā ti-ra-ki-ṭa dhin dhin | dhā dhā dhin dhin", 50,
                                "16 mātrās, 4+4+4+4: vilambit khayāl."));
            all.push_back (tal ("sūltāl", "sultal", "x 0 2 3 0",
                                "dhā dhā | din tā | ki-ṭa dhā | ti-ṭa ka-ta | ga-di ga-na", 100,
                                "10 mātrās in five vibhāgs of two: pakhāvaj, for dhrupad."));
            all.push_back (tal ("tīvrā", "tivra", "x 2 3",
                                "dhā din tā | ti-ṭa ka-ta | ga-di ga-na", 120,
                                "7 mātrās, 3+2+2: pakhāvaj, for dhrupad."));
            all.push_back (tal ("jat tāl", "jat tal", "x 2 0 3",
                                "dhā – dhī – | dhā dhā tī – | tā – tī – | dhā dhā dhī –", 90,
                                "16 mātrās, 4+4+4+4: ṭhumrī and folk songs."));
            all.push_back (tal ("pañjābī", "panjabi", "x 2 0 3",
                                "dhā –-dhi –-ka dhā | dhā –-dhi –-ka dhā | dhā –-ti –-ka tā | tā –-dhi –-ka dhā", 110,
                                "16 mātrās, 4+4+4+4, also called sitārkhānī: tīntāl's lilting cousin for ṭhumrī and gazal."));
        }

        //======================================================================
        // South Indian tāḷams: the seven sūḷādi tāḷas, each in the five jātis (the laghu's
        // length), and the cāpu tāḷas. The kriyās (hand gestures) as sounds: a clap, the
        // finger counts of a laghu, the wave (vīccu) of a drutam, and the gati's four pulses
        // to the akṣara (caturaśra naḍai) under them.
        struct Jati { const char* name; const char* plain; int beats; };
        const Jati jatis[] { { "tiśra", "tisra", 3 }, { "caturaśra", "caturasra", 4 }, { "khaṇḍa", "khanda", 5 },
                             { "miśra", "misra", 7 }, { "saṅkīrṇa", "sankirna", 9 } };

        Rhythm carnatic (std::string name, std::string plain, std::string subgroup, const std::vector<int>& sections,
                         const std::vector<std::string>& kinds, double bpm, std::string about)
        {
            Rhythm r;
            r.name = std::move (name);
            r.plain = std::move (plain);
            r.group = "south indian tāḷam";
            r.subgroup = std::move (subgroup);
            r.sections = sections;
            r.beats = std::accumulate (sections.begin(), sections.end(), 0);
            r.bpm = bpm;
            r.about = std::move (about);
            const auto n = (size_t) r.beats;
            std::string claps (n, '.'), counts (n, '.'), waves (n, '.'), anga (n, '.');
            size_t start = 0;
            for (size_t a = 0; a < sections.size(); ++a)
            {
                const auto& kind = kinds[a];
                const auto len = (size_t) sections[a];
                claps[start] = a == 0 ? 'X' : 'x';
                anga[start] = a == 0 ? 'X' : 'x';
                if (a > 0)
                    r.counting += " | ";
                if (kind == "drutam")
                {
                    waves[start + 1] = 'x';
                    r.counting += "drutam: clap, wave";
                }
                else if (kind == "anudrutam")
                    r.counting += "anudrutam: clap";
                else if (kind == "laghu")
                {
                    for (size_t k = 1; k < len; ++k)
                        counts[start + k] = 'o';
                    r.counting += "laghu (" + std::to_string (len) + "): clap, " + std::to_string (len - 1) + " finger counts";
                }
                else   // a cāpu's beat groups, counted in solkaṭṭu
                    r.counting += kind;
                start += len;
            }
            r.lanes.push_back (lane ("clap (taṭṭu)", gm::clap, 1, claps));
            if (counts.find_first_not_of ('.') != std::string::npos)
                r.lanes.push_back (lane ("finger counts", gm::sideStick, 1, counts));
            if (waves.find_first_not_of ('.') != std::string::npos)
                r.lanes.push_back (lane ("wave (vīccu)", gm::hiWoodBlock, 1, waves));
            r.lanes.push_back (lane ("aṅga (samam accented)", gm::lowConga, 1, anga));
            r.lanes.push_back (lane ("gati: ta ka dhi mi", gm::muteHiConga, 4, "X" + std::string (n * 4 - 1, '.')));
            auto& gati = r.lanes.back().hits;
            for (size_t b = 1; b < n * 4; ++b)
                gati[b] = b % 4 == 0 ? 'x' : 'o';
            return r;
        }

        void addTalams (std::vector<Rhythm>& all)
        {
            struct Tala { const char* name; const char* plain; const char* angas; int usual; };   // L laghu, O drutam, U anudrutam
            const Tala talas[] { { "dhruva", "dhruva", "LOLL", 4 }, { "maṭhya", "mathya", "LOL", 4 },
                                 { "rūpaka", "rupaka", "OL", 4 }, { "jhampa", "jhampa", "LUO", 7 },
                                 { "tripuṭa", "triputa", "LOO", 3 }, { "aṭa", "ata", "LLOO", 5 }, { "ēka", "eka", "L", 4 } };
            for (const auto& t : talas)
                for (const auto& j : jatis)
                {
                    std::vector<int> sections;
                    std::vector<std::string> kinds;
                    for (const char* c = t.angas; *c != 0; ++c)
                    {
                        sections.push_back (*c == 'L' ? j.beats : *c == 'O' ? 2 : 1);
                        kinds.push_back (*c == 'L' ? "laghu" : *c == 'O' ? "drutam" : "anudrutam");
                    }
                    const bool adi = std::string (t.plain) == "triputa" && j.beats == 4;
                    auto name = std::string (j.name) + " jāti " + t.name + " tāḷam";
                    auto plain = std::string (j.plain) + " jati " + t.plain + " talam";
                    if (adi)
                    {
                        name += " (ādi tāḷam)";
                        plain += " (adi talam)";
                    }
                    const auto beats = std::accumulate (sections.begin(), sections.end(), 0);
                    std::string about = std::to_string (beats) + " akṣaras.";
                    if (adi)
                        about += " Ādi tāḷam, the commonest tāḷam of Karnatak music.";
                    else if (j.beats == t.usual)
                        about += " The jāti usually meant by the bare name " + std::string (t.name) + " tāḷam.";
                    all.push_back (carnatic (name, plain, t.name, sections, kinds, adi ? 70.0 : 80.0, about));
                }
            all.push_back (carnatic ("miśra cāpu", "misra capu", "cāpu", { 3, 2, 2 }, { "ta ki ṭa", "ta ka", "dhi mi" }, 120,
                                     "7 beats, 3+2+2: kṛtis and padams; quick, and counted in claps."));
            all.push_back (carnatic ("khaṇḍa cāpu", "khanda capu", "cāpu", { 2, 3 }, { "ta ka", "ta ki ṭa" }, 120,
                                     "5 beats, 2+3."));
            all.push_back (carnatic ("tiśra cāpu", "tisra capu", "cāpu", { 1, 2 }, { "ta", "ki ṭa" }, 120,
                                     "3 beats, 1+2: folk and devotional songs."));
            all.push_back (carnatic ("saṅkīrṇa cāpu", "sankirna capu", "cāpu", { 4, 5 }, { "ta ka dhi mi", "ta ka ta ki ṭa" }, 120,
                                     "9 beats, 4+5."));
        }

        //======================================================================
        // Classic breakbeats: the main bars as drum machines are usually programmed from the
        // records, simplified (the drummers vary them bar to bar). 16ths, 4/4.
        Rhythm groove (std::string name, std::string plain, std::string group, std::string ipa, int beats,
                       std::vector<int> sections, double bpm, std::string about, std::vector<Lane> lanes, std::string counting = {})
        {
            Rhythm r;
            r.name = std::move (name);
            r.plain = std::move (plain);
            r.group = std::move (group);
            r.ipa = std::move (ipa);
            r.beats = beats;
            r.sections = std::move (sections);
            r.bpm = bpm;
            r.about = std::move (about);
            r.lanes = std::move (lanes);
            r.counting = std::move (counting);
            return r;
        }

        void addBreaks (std::vector<Rhythm>& all)
        {
            const std::string g = "breakbeats";
            const auto eighths = rep ("x.", 8), sixteenths = rep ("x", 16);
            all.push_back (groove ("amen break", "amen break", g, "ˈɑːˈmɛn breɪk", 16, { 4, 4, 4, 4 }, 136,
                "The Winstons, \"Amen, Brother\" (1969), drums by Gregory C. Coleman: four bars, the last with the crash.",
                { lane ("kick", gm::kick, 4, "x.x.......xx.... x.x.......xx.... x.x.......x..... ..xx......x....."),
                  lane ("snare", gm::snare, 4, "....x..x.x..x..x ....x..x.x..x..x ....x..x.x....x. .x..x..x.x....x."),
                  lane ("ride", gm::ride, 4, eighths + eighths + eighths + "x.x.x.x.x...x.x."),
                  lane ("crash", gm::crash, 4, rep (".", 48) + "..........x.....") }));
            all.push_back (groove ("funky drummer", "funky drummer", g, "ˈfʌŋki ˈdrʌmər", 4, { 4 }, 100,
                "James Brown, \"Funky Drummer\" (1970), drums by Clyde Stubblefield.",
                { lane ("kick", gm::kick, 4, "x.x...x...x..x.."),
                  lane ("snare", gm::snare, 4, "....X..o.o.oX..o"),
                  lane ("closed hi-hat", gm::closedHat, 4, "xxxxxxx.xxxxxxxx"),
                  lane ("open hi-hat", gm::openHat, 4, ".......x........") }));
            all.push_back (groove ("think (about it)", "think about it", g, "θɪŋk əˈbaʊt ɪt", 4, { 4 }, 112,
                "Lyn Collins, \"Think (About It)\" (1972).",
                { lane ("kick", gm::kick, 4, "x......xx.x....."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, eighths) }));
            all.push_back (groove ("apache", "apache", g, "əˈpætʃi", 4, { 4 }, 118,
                "The Incredible Bongo Band, \"Apache\" (1973): kit and bongos.",
                { lane ("kick", gm::kick, 4, "x.....x...x....."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, eighths),
                  lane ("high bongo", gm::hiBongo, 4, "x.xx..x.x.xx..x."),
                  lane ("low bongo", gm::lowBongo, 4, "...x...x...x...x") }));
            all.push_back (groove ("impeach the president", "impeach the president", g, "ɪmˈpiːtʃ ðə ˈprɛzɪdənt", 4, { 4 }, 96,
                "The Honey Drippers, \"Impeach the President\" (1973).",
                { lane ("kick", gm::kick, 4, "x......x..x....."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, "x.x.x.x.x.x.x..."),
                  lane ("open hi-hat", gm::openHat, 4, "..............x.") }));
            all.push_back (groove ("when the levee breaks", "when the levee breaks", g, "wɛn ðə ˈlɛvi breɪks", 4, { 4 }, 71,
                "Led Zeppelin, \"When the Levee Breaks\" (1971), drums by John Bonham.",
                { lane ("kick", gm::kick, 4, "xx.....x..xx...."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, eighths) }));
            all.push_back (groove ("funky president", "funky president", g, "ˈfʌŋki ˈprɛzɪdənt", 4, { 4 }, 98,
                "James Brown, \"Funky President (People It's Bad)\" (1974).",
                { lane ("kick", gm::kick, 4, "x..x......x..x.."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, sixteenths) }));
            all.push_back (groove ("cold sweat", "cold sweat", g, "koʊld swɛt", 4, { 4 }, 112,
                "James Brown, \"Cold Sweat\" (1967), drums by Clyde Stubblefield.",
                { lane ("kick", gm::kick, 4, "x.......x.x....."),
                  lane ("snare", gm::snare, 4, "....x..x.o..x..o"),
                  lane ("closed hi-hat", gm::closedHat, 4, eighths) }));
            all.push_back (groove ("it's a new day", "its a new day", g, "ɪts ə nuː deɪ", 4, { 4 }, 92,
                "Skull Snaps, \"It's a New Day\" (1973).",
                { lane ("kick", gm::kick, 4, "x.x...x..x......"),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, eighths) }));
            all.push_back (groove ("the big beat", "the big beat", g, "ðə bɪɡ biːt", 4, { 4 }, 100,
                "Billy Squier, \"The Big Beat\" (1980).",
                { lane ("kick", gm::kick, 4, "x......x..x....."),
                  lane ("snare", gm::snare, 4, "....X.......X..."),
                  lane ("closed hi-hat", gm::closedHat, 4, eighths) }));
        }

        //======================================================================
        void addTrap (std::vector<Rhythm>& all)
        {
            const std::string g = "trap";
            all.push_back (groove ("trap (half-time)", "trap half-time", g, "træp (ˈhæfˌtaɪm)", 8, { 4, 4 }, 140,
                "Atlanta trap: the clap on the third beat (half time), 808 kicks between, 16th hats rolling into 32nds.",
                { lane ("808 kick", gm::kick, 4, "x......x..x..... x.x.......x....."),
                  lane ("clap", gm::clap, 4, "........x....... ........x......."),
                  lane ("closed hi-hat", gm::closedHat, 8, rep ("x.x.", 8) + rep ("x.x.", 6) + "xxxxxxxx") }));
            all.push_back (groove ("trap (triplet hats)", "trap triplet hats", g, "træp (ˈtrɪplɪt hæts)", 8, { 4, 4 }, 140,
                "Trap with the hats in 16th-note triplets, rolling faster at the end of the second bar.",
                { lane ("808 kick", gm::kick, 4, "x.....x...x..... x.......x.x....."),
                  lane ("clap", gm::clap, 4, "........x....... ........x......."),
                  lane ("closed hi-hat", gm::closedHat, 6, rep ("x.x", 8) + rep ("x.x", 4) + rep ("x", 12)),
                  lane ("open hi-hat", gm::openHat, 4, "..............x. ................") }));
            all.push_back (groove ("drill", "drill", g, "drɪl", 8, { 4, 4 }, 142,
                "UK and New York drill: swung triplet hats, the snare on the third beat and late in the second bar.",
                { lane ("808 kick", gm::kick, 4, "x.........x..... ..x.....x......."),
                  lane ("snare", gm::snare, 4, "........x....... ........x....x.."),
                  lane ("closed hi-hat", gm::closedHat, 6, rep ("x.xx.x", 8)) }));
        }

        //======================================================================
        // The six rhythmic modes of the Notre-Dame school, as Johannes de Garlandia set them
        // out: longs (L) and breves (B) in ternary time, a perfection (three tempora) to the
        // beat. Played on a tabor and sung on d, a mode's ordo twice.
        void addMedieval (std::vector<Rhythm>& all)
        {
            struct M { const char* name; const char* plain; const char* hits; const char* counting; const char* about; };
            const M modes[] {
                { "modus primus (long, breve)", "modus primus", "X-xx-x", "L B | L B", "Trochaic: a long of two tempora, a breve of one." },
                { "modus secundus (breve, long)", "modus secundus", "Xx-xx-", "B L | B L", "Iambic: a breve, then a long of two tempora." },
                { "modus tertius (long, breve, breve)", "modus tertius", "X--xx-", "L. B B(altera)", "Dactylic: a perfect long, a breve, and a breve altera of two tempora." },
                { "modus quartus (breve, breve, long)", "modus quartus", "Xx-x--", "B B(altera) L.", "Anapaestic: the third mode turned round; rare in the sources." },
                { "modus quintus (longs)", "modus quintus", "X--x--", "L. | L.", "Spondaic: perfect longs, three tempora each; the tenors of organa and motets." },
                { "modus sextus (breves)", "modus sextus", "Xxxxxx", "B B B | B B B", "Tribrachic: breves throughout." },
            };
            for (const auto& m : modes)
            {
                Rhythm r = groove (m.name, m.plain, "medieval rhythmic modes", {}, 2, { 1, 1 }, 60, m.about,
                                   { lane ("tabor", gm::lowTom, 3, m.hits) }, m.counting);
                Lane voice { "voice (d)", 62, 3, m.hits, 0 };
                r.lanes.push_back (voice);
                all.push_back (std::move (r));
            }
        }

        //======================================================================
        void addCaribbean (std::vector<Rhythm>& all)
        {
            const std::string g = "caribbean";
            all.push_back (groove ("son clave (3-2)", "son clave 3-2", g, {}, 4, { 2, 2 }, 100,
                "Cuba: the son's timeline, three strokes then two.", { lane ("claves", gm::claves, 4, "x..x..x...x.x...") }));
            all.push_back (groove ("son clave (2-3)", "son clave 2-3", g, {}, 4, { 2, 2 }, 100,
                "Cuba: the son clave from its other side.", { lane ("claves", gm::claves, 4, "..x.x...x..x..x.") }));
            all.push_back (groove ("rumba clave (3-2)", "rumba clave 3-2", g, {}, 4, { 2, 2 }, 100,
                "Cuba: the rumba's clave, its third stroke a 16th later than the son's.", { lane ("claves", gm::claves, 4, "x..x...x..x.x...") }));
            all.push_back (groove ("rumba clave (2-3)", "rumba clave 2-3", g, {}, 4, { 2, 2 }, 100,
                "Cuba: the rumba clave from its other side.", { lane ("claves", gm::claves, 4, "..x.x...x..x...x") }));
            all.push_back (groove ("tresillo", "tresillo", g, {}, 4, { 2, 2 }, 100,
                "Cuba and across the Americas: 3+3+2, twice to the bar.", { lane ("bass drum", gm::kick, 4, "x..x..x.x..x..x.") }));
            all.push_back (groove ("cinquillo", "cinquillo", g, {}, 4, { 2, 2 }, 100,
                "Cuba: the danzón's five-stroke figure.", { lane ("güiro", gm::shortGuiro, 4, "x.xx.xx.x.xx.xx.") }));
            all.push_back (groove ("habanera", "habanera", g, {}, 4, { 2, 2 }, 90,
                "Cuba, 19th century: the contradanza's bass, behind the tango and much else.", { lane ("bass drum", gm::kick, 4, "x..xx.x.x..xx.x.") }));
            all.push_back (groove ("son montuno (tumbao, 2-3)", "son montuno", g, {}, 8, { 4, 4 }, 190,
                "Cuba: the conga's tumbao, open tones on four and its 'and', over a 2-3 son clave and the bongó's martillo.",
                { lane ("claves", gm::claves, 4, "....x...x.....x. x.....x...x....."),
                  lane ("conga, heel and toe", gm::muteHiConga, 4, "o.o...o.o.o...o. o.o...o.o.o...o."),
                  lane ("conga, slap", gm::muteHiConga, 4, "....X.......X... ....X.......X..."),
                  lane ("conga, open", gm::openHiConga, 4, "...........x.x.. ...........x.x.."),
                  lane ("bongó (martillo)", gm::hiBongo, 4, "xoxoxoxoxoxoxoxo xoxoxoxoxoxoxoxo"),
                  lane ("bongó, open", gm::lowBongo, 4, "...x.......x.... ...x.......x....") }));
            all.push_back (groove ("cha-cha-chá", "cha-cha-cha", g, {}, 4, { 4 }, 116,
                "Cuba, 1950s (Enrique Jorrín): the cowbell on the beat, the güiro's long and short strokes, the conga's 'cha-cha' into the bar.",
                { lane ("cowbell", gm::cowbell, 4, "x...x...x...x..."),
                  lane ("güiro, long", gm::longGuiro, 4, "x.......x......."),
                  lane ("güiro, short", gm::shortGuiro, 4, "....x.x.....x.x."),
                  lane ("conga", gm::openHiConga, 4, "............x.x.") }));
            all.push_back (groove ("bembé (6/8)", "bembe", g, {}, 4, { 2, 2 }, 110,
                "Afro-Cuban: the bell's seven strokes in twelve pulses, the West African standard pattern.",
                { lane ("bell", gm::cowbell, 3, "x.x.xx.x.x.x"),
                  lane ("shekere", gm::cabasa, 3, "x..x..x..x..") }));
            all.push_back (groove ("one drop", "one drop", g, {}, 4, { 4 }, 76,
                "Jamaica, reggae: the bass drum and rimshot together on the third beat only.",
                { lane ("kick", gm::kick, 4, "........x......."),
                  lane ("rimshot", gm::sideStick, 4, "........x......."),
                  lane ("closed hi-hat", gm::closedHat, 4, "x.x.x.x.x.x.x.x.") }));
            all.push_back (groove ("rockers", "rockers", g, {}, 4, { 4 }, 80,
                "Jamaica, mid-1970s (Sly Dunbar): the bass drum on one and three.",
                { lane ("kick", gm::kick, 4, "x.......x......."),
                  lane ("rimshot", gm::sideStick, 4, "........x......."),
                  lane ("closed hi-hat", gm::closedHat, 4, "x.xxx.xxx.xxx.xx") }));
            all.push_back (groove ("steppers", "steppers", g, {}, 4, { 4 }, 80,
                "Jamaica and UK dub: the bass drum on every beat.",
                { lane ("kick", gm::kick, 4, "x...x...x...x..."),
                  lane ("rimshot", gm::sideStick, 4, "........x......."),
                  lane ("closed hi-hat", gm::closedHat, 4, "..x...x...x...x.") }));
            all.push_back (groove ("ska", "ska", g, {}, 4, { 4 }, 150,
                "Jamaica, early 1960s: the hats on the off-beats with the guitar's skank.",
                { lane ("kick", gm::kick, 4, "x.......x......."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("closed hi-hat", gm::closedHat, 4, "..x...x...x...x.") }));
            all.push_back (groove ("dembow", "dembow", g, {}, 4, { 4 }, 95,
                "Jamaica to Puerto Rico and Panama: Shabba Ranks's \"Dem Bow\" (1990) riddim, the heart of reggaetón.",
                { lane ("kick", gm::kick, 4, "x...x...x...x..."),
                  lane ("snare", gm::snare, 4, "...x..x....x..x."),
                  lane ("closed hi-hat", gm::closedHat, 4, "x.x.x.x.x.x.x.x.") }));
            all.push_back (groove ("soca", "soca", g, {}, 4, { 4 }, 155,
                "Trinidad, 1970s (Lord Shorty): four on the floor, the snare on two and four, the cowbell on the off-beats.",
                { lane ("kick", gm::kick, 4, "x...x...x...x..."),
                  lane ("snare", gm::snare, 4, "....x.......x..."),
                  lane ("cowbell", gm::cowbell, 4, "..x...x...x...x."),
                  lane ("closed hi-hat", gm::closedHat, 4, "xxxxxxxxxxxxxxxx") }));
            all.push_back (groove ("merengue", "merengue", g, {}, 4, { 4 }, 150,
                "Dominican Republic: the güira scraping steadily, the tambora's slap closing each bar (simplified).",
                { lane ("güira", gm::cabasa, 4, "X.xxX.xxX.xxX.xx"),
                  lane ("tambora, skin", gm::lowTom, 4, "x.......x......x"),
                  lane ("tambora, rim", gm::sideStick, 4, "...x.x.x...x.x..") }));
        }

        //======================================================================
        void addSouthAmerican (std::vector<Rhythm>& all)
        {
            const std::string g = "south american";
            all.push_back (groove ("samba (batucada)", "samba", g, {}, 4, { 2, 2 }, 100,
                "Brazil: two bars of 2/4, the surdos answering (the low one on the second beat), the caixa's 16ths, the tamborim's teleco-teco (simplified).",
                { lane ("surdo de primeira", gm::floorTom, 4, "....X.......X..."),
                  lane ("surdo de segunda", gm::lowTom, 4, "x.......x......."),
                  lane ("caixa", gm::snare, 4, "xoxxxoxxxoxxxoxx"),
                  lane ("tamborim", gm::hiWoodBlock, 4, "x.x..x.x.xx.x.x."),
                  lane ("ganzá", gm::maracas, 4, "Xooo" "Xooo" "Xooo" "Xooo") }));
            all.push_back (groove ("bossa nova", "bossa nova", g, {}, 8, { 4, 4 }, 130,
                "Brazil, late 1950s: the rim's bossa clave over two bars, the bass drum's dotted figure, 8th-note hats.",
                { lane ("rimshot", gm::sideStick, 4, "x..x..x...x..x.. x..x..x...x..x.."),
                  lane ("kick", gm::kick, 4, "x..xx..xx..xx..x x..xx..xx..xx..x"),
                  lane ("closed hi-hat", gm::closedHat, 4, rep ("x.", 16)) }));
            all.push_back (groove ("baião", "baiao", g, {}, 4, { 2, 2 }, 120,
                "Northeast Brazil (Luiz Gonzaga): the zabumba's dotted beat, the triangle open and closed.",
                { lane ("zabumba", gm::floorTom, 4, "X..x....X..x...."),
                  lane ("bacalhau", gm::sideStick, 4, "....x.x.....x.x."),
                  lane ("triangle, closed", gm::muteTriangle, 4, "x.x.x.x.x.x.x.x."),
                  lane ("triangle, open", gm::openTriangle, 4, ".x.x.x.x.x.x.x.x") }));
            all.push_back (groove ("tango (3-3-2)", "tango 3-3-2", g, {}, 4, { 4 }, 120,
                "Argentina and Uruguay: the 3-3-2 of the orchestra's bass and chords; on the beat it is the marcato.",
                { lane ("bass", gm::kick, 4, "x..x..x.x..x..x."),
                  lane ("chicharra", gm::sideStick, 4, "....x.......x...") }));
            all.push_back (groove ("milonga", "milonga", g, {}, 4, { 2, 2 }, 100,
                "Argentina and Uruguay: the habanera's lilt as 3-3-2 in 2/4.",
                { lane ("bass", gm::kick, 4, "x..x..x.x..x..x."),
                  lane ("guitar strum", gm::sideStick, 4, ".x...x.x.x...x.x") }));
            all.push_back (groove ("candombe", "candombe", g, {}, 4, { 2, 2 }, 110,
                "Uruguay: the madera on the drums' shells, the chico's steady figure, the piano drum's beat (simplified).",
                { lane ("madera", gm::claves, 4, "x..x...x..x.x..."),
                  lane ("chico", gm::muteHiConga, 4, ".xx..xx..xx..xx."),
                  lane ("piano", gm::lowConga, 4, "x.....x.x.....x.") }));
            all.push_back (groove ("cumbia", "cumbia", g, {}, 4, { 4 }, 95,
                "Colombia: the llamador on the off-beats, the guacharaca's long and short scrapes, the alegre's calls.",
                { lane ("llamador", gm::lowConga, 4, "..x...x...x...x."),
                  lane ("guacharaca, long", gm::longGuiro, 4, "x...x...x...x..."),
                  lane ("guacharaca, short", gm::shortGuiro, 4, "..xx..xx..xx..xx"),
                  lane ("tambor alegre", gm::openHiConga, 4, "x..x..x.x..x....") }));
        }

        //======================================================================
        // Timelines counted in pulses, as Toussaint gives them, with a drum on the beat.
        void addAfrican (std::vector<Rhythm>& all)
        {
            const std::string w = "west african";
            all.push_back (groove ("agbekɔ (standard bell)", "agbekor", w, {}, 4, { 1, 1, 1, 1 }, 110,
                "Ewe, Ghana and Togo: the gankogui's seven strokes in twelve pulses, the standard pattern of West African 12/8, with the axatse rattle.",
                { lane ("gankogui (bell)", gm::cowbell, 3, "X.x.xx.x.x.x"),
                  lane ("axatse (rattle)", gm::cabasa, 3, "x.xx.xx.xx.x"),
                  lane ("kagan", gm::muteHiConga, 3, ".xx.xx.xx.xx") }));
            all.push_back (groove ("fume-fume", "fume-fume", w, {}, 4, { 1, 1, 1, 1 }, 110,
                "Ghana: the five-stroke 12/8 bell.", { lane ("bell", gm::cowbell, 3, "x.x.x..x.x..") }));
            all.push_back (groove ("gahu", "gahu", w, {}, 4, { 4 }, 120,
                "Ewe, Ghana and Benin: the gankogui's 4/4 timeline.",
                { lane ("bell", gm::cowbell, 4, "x..x..x...x...x."),
                  lane ("shaker", gm::cabasa, 4, "..x...x...x...x.") }));
            all.push_back (groove ("shiko", "shiko", w, {}, 4, { 4 }, 110,
                "Nigeria: the five-stroke timeline.", { lane ("bell", gm::cowbell, 4, "x...x.x...x.x...") }));
            all.push_back (groove ("soukous", "soukous", w, {}, 4, { 4 }, 130,
                "Congo, carried across West Africa: the five-stroke timeline.", { lane ("bell", gm::cowbell, 4, "x..x..x...xx....") }));
            all.push_back (groove ("afrobeat", "afrobeat", w, "ˈæfroʊbiːt", 4, { 4 }, 110,
                "Nigeria, 1970s (Fela Kuti's Africa 70, drums by Tony Allen): the kick and snare syncopated round busy hats (simplified).",
                { lane ("kick", gm::kick, 4, "x..x......x..x.."),
                  lane ("snare", gm::snare, 4, "..o.X.o..o.oX.o."),
                  lane ("closed hi-hat", gm::closedHat, 4, "xxxx.xxxxxxx.xxx"),
                  lane ("open hi-hat", gm::openHat, 4, "....x.......x...") }));

            // North African: Egyptian and Maghrebi iqāʿāt, dum (the darbuka's middle) and tak (its rim).
            const std::string n = "north african";
            struct Iqa { const char* name; const char* plain; int beats; const char* dum; const char* tak; const char* counting; const char* about; double bpm; };
            const Iqa iqaat[] {
                { "maqsūm", "maqsum", 4, "x...x...", ".x.x..x.", "dum tak – tak dum – tak –", "Egypt: the commonest iqāʿ of popular music, 4/4.", 110 },
                { "baladī", "baladi", 4, "xx..x...", "...x..x.", "dum dum – tak dum – tak –", "Egypt: maqsūm with two dums to begin, 4/4.", 110 },
                { "ṣaʿīdī", "saidi", 4, "x..xx...", ".x....x.", "dum tak – dum dum – tak –", "Upper Egypt: the stick dance's iqāʿ, 4/4.", 110 },
                { "malfūf", "malfuf", 2, "x.......", "...x..x.", "dum – – tak – – tak –", "Egypt: quick 2/4, for entrances.", 120 },
                { "ayyūb", "ayyub", 2, "x...x...", "...x..x.", "dum – – ka dum – tak –", "Egypt and Sudan: the zār's driving 2/4.", 120 },
            };
            for (const auto& q : iqaat)
            {
                const auto perBeat = (int) std::string (q.dum).size() / q.beats;
                all.push_back (groove (q.name, q.plain, n, {}, q.beats, { q.beats }, q.bpm, q.about,
                                       { lane ("dum", gm::lowConga, perBeat, q.dum), lane ("tak", gm::openHiConga, perBeat, q.tak) }, q.counting));
            }
            all.push_back (groove ("gnāwa (qarāqib)", "gnawa", n, {}, 4, { 1, 1, 1, 1 }, 100,
                "Morocco: the qarāqib (iron castanets) of the Gnawa līla, three strokes to the beat, long-short-short (simplified).",
                { lane ("qarāqib", gm::maracas, 3, "X.xx.xx.xx.x"),
                  lane ("gumbrī (bass)", gm::lowConga, 3, "x.....x..x..") }));
        }

        //======================================================================
        // Korean jangdan on the janggu: kung on the left head (gungpyeon), deok on the right
        // (chaepyeon), deong both together, gi a grace note and deoreoreo a roll, by pulse.
        Rhythm jangdan (std::string name, std::string about, int beats, int perBeat, std::vector<int> sections, double bpm,
                        std::initializer_list<int> deong, std::initializer_list<int> kung, std::initializer_list<int> deok,
                        std::initializer_list<int> gi, std::initializer_list<int> rolls, std::string counting)
        {
            const auto n = beats * perBeat;
            std::string left ((size_t) n, '.'), right ((size_t) n, '.'), roll ((size_t) n * 4, '.');
            for (auto p : deong) { left[(size_t) p - 1] = 'X'; right[(size_t) p - 1] = 'X'; }
            for (auto p : kung) left[(size_t) p - 1] = 'x';
            for (auto p : deok) right[(size_t) p - 1] = 'x';
            for (auto p : gi) roll[(size_t) (p - 1) * 4 - 1] = 'o';
            for (auto p : rolls)
                for (int k = 0; k < 3; ++k)
                    roll[(size_t) (p - 1) * 4 + (size_t) k] = 'o';
            std::vector<Lane> lanes { lane ("gungpyeon (kung)", gm::lowConga, perBeat, left),
                                      lane ("chaepyeon (deok)", gm::highTimbale, perBeat, right) };
            if (roll.find_first_not_of ('.') != std::string::npos)
                lanes.push_back (lane ("chaepyeon (gi, deoreoreo)", gm::highTimbale, perBeat * 4, roll));
            auto plain = name;
            return groove (std::move (name), std::move (plain), "east asian", {}, beats, std::move (sections), bpm, std::move (about),
                           std::move (lanes), std::move (counting));
        }

        void addKorean (std::vector<Rhythm>& all)
        {
            all.push_back (jangdan ("jungmori", "Korea: the moderate 12-beat jangdan of pansori and sanjo.",
                                    12, 1, { 3, 3, 3, 3 }, 80, { 1 }, { 4, 7, 8, 10, 12 }, { 3, 6, 9 }, {}, {},
                                    "deong – deok kung – deok kung kung deok kung – kung"));
            all.push_back (jangdan ("jungjungmori", "Korea: livelier, four beats of three pulses (12/8).",
                                    4, 3, { 1, 1, 1, 1 }, 70, { 1 }, { 4, 7, 8, 10 }, { 3, 5, 6, 9, 11 }, {}, {},
                                    "deong – deok | kung deok deok | kung kung deok | kung deok –"));
            all.push_back (jangdan ("jajinmori", "Korea: fast, four beats of three pulses (12/8).",
                                    4, 3, { 1, 1, 1, 1 }, 120, { 1 }, { 4, 7, 10 }, { 9 }, {}, {},
                                    "deong – – | kung – – | kung – deok | kung – –"));
            all.push_back (jangdan ("gutgeori", "Korea: the shaman ritual's and folk song's lilting 12/8.",
                                    4, 3, { 1, 1, 1, 1 }, 80, { 1 }, { 4, 7, 10 }, { 3, 9 }, { 3, 9 }, { 5, 11 },
                                    "deong – gideok | kung deoreoreo – | kung – gideok | kung deoreoreo –"));
            all.push_back (jangdan ("semachi", "Korea: three beats of three pulses (9/8), for folk song.",
                                    3, 3, { 1, 1, 1 }, 90, { 1, 4 }, { 7 }, { 6, 8 }, {}, {},
                                    "deong – – | deong – deok | kung deok –"));
            all.push_back (jangdan ("eotmori", "Korea: ten pulses, 3+2+3+2 (10/8), for sanjo and shaman music.",
                                    10, 1, { 3, 2, 3, 2 }, 200, { 1 }, { 3, 6, 8 }, { 4, 9 }, {}, {},
                                    "deo-eong kung | ttak – | ku-ung kung | ttak –"));
            all.push_back (jangdan ("hwimori", "Korea: the fastest, four beats of two (4/4); danmori is it faster still.",
                                    4, 2, { 1, 1, 1, 1 }, 160, { 1 }, { 5, 7 }, { 3, 4, 6 }, {}, {},
                                    "deong – | deok deok | kung deok | kung –"));
        }

        std::vector<Rhythm> build()
        {
            std::vector<Rhythm> all;
            addTals (all);
            addTalams (all);
            addBreaks (all);
            addTrap (all);
            addMedieval (all);
            addCaribbean (all);
            addSouthAmerican (all);
            addAfrican (all);
            addKorean (all);
            return all;
        }

        std::string compact (const std::string& hits)
        {
            std::string s;
            for (auto c : hits)
                if (c != ' ' && c != '|')
                    s += c;
            return s;
        }
    }

    const std::vector<Rhythm>& rhythms()
    {
        static const auto all = build();
        return all;
    }

    int findRhythm (const std::string& plain)
    {
        const auto& all = rhythms();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].plain == plain)
                return (int) i;
        return -1;
    }

    std::vector<std::string> groups()
    {
        std::vector<std::string> out;
        for (const auto& r : rhythms())
            if (std::find (out.begin(), out.end(), r.group) == out.end())
                out.push_back (r.group);
        return out;
    }

    int steps (const Lane& l) { return (int) compact (l.hits).size(); }

    std::vector<Hit> hits (const Rhythm& r)
    {
        std::vector<Hit> out;
        for (size_t i = 0; i < r.lanes.size(); ++i)
        {
            const auto s = compact (r.lanes[i].hits);
            for (size_t k = 0; k < s.size(); ++k)
            {
                const auto c = s[k];
                if (c != 'X' && c != 'x' && c != 'o')
                    continue;
                Hit h;
                h.lane = (int) i;
                h.step = (int) k;
                h.velocity = c == 'X' ? 120 : c == 'x' ? 96 : 56;
                for (auto j = k + 1; j < s.size() && s[j] == '-'; ++j)
                    ++h.length;
                out.push_back (h);
            }
        }
        return out;
    }

    int rowsPerBeat (const Rhythm& r)
    {
        int n = 1;
        for (const auto& l : r.lanes)
            n = std::lcm (n, std::max (1, l.perBeat));
        return n;
    }

    bool valid (const Rhythm& r)
    {
        if (r.beats <= 0 || r.lanes.empty() || std::accumulate (r.sections.begin(), r.sections.end(), 0) != r.beats)
            return false;
        for (const auto& l : r.lanes)
        {
            const auto s = compact (l.hits);
            if (l.perBeat <= 0 || (int) s.size() != r.beats * l.perBeat || s.find_first_not_of ("Xxo-.") != std::string::npos)
                return false;
        }
        return true;
    }

    midifile::SongPtr toSong (const Rhythm& r, int cycles, double bpm)
    {
        constexpr int tpq = 480;
        auto song = std::make_shared<midifile::Song>();
        song->format = 0;
        song->ticksPerQuarter = tpq;
        song->tempos.push_back ({ 0, 60.0e6 / std::max (1.0, bpm) });
        song->trackNames.push_back (r.name);
        cycles = std::max (1, cycles);
        const auto cycleTicks = (int64_t) r.beats * tpq;
        struct Timed { int64_t tick; int order; midifile::Event e; };   // offs (0) before ons (1) at the same tick
        std::vector<Timed> events;
        for (const auto& h : hits (r))
        {
            const auto& l = r.lanes[(size_t) h.lane];
            const auto stepTicks = (int64_t) tpq / std::max (1, l.perBeat);
            const auto channel = (uint8_t) (l.channel & 0x0F);
            // A drum is short (most kits ignore the length); a held note lasts its holds.
            const auto length = l.channel == 9 ? std::min<int64_t> (stepTicks, tpq / 4) : stepTicks * h.length - 1;
            for (int c = 0; c < cycles; ++c)
            {
                const auto start = c * cycleTicks + (int64_t) h.step * tpq / l.perBeat;
                events.push_back ({ start, 1, { start, (uint8_t) (0x90 | channel), (uint8_t) l.note, (uint8_t) h.velocity } });
                events.push_back ({ start + length, 0, { start + length, (uint8_t) (0x80 | channel), (uint8_t) l.note, 0 } });
                song->channelsUsed |= (uint16_t) (1u << channel);
            }
        }
        std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b)
        {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        for (const auto& t : events)
            song->events.push_back (t.e);
        song->lastTick = cycles * cycleTicks;
        return song;
    }

    std::string drumName (int note)
    {
        static const char* const names[] {
            "acoustic bass drum", "bass drum", "side stick", "acoustic snare", "hand clap", "electric snare", "low floor tom",
            "closed hi-hat", "high floor tom", "pedal hi-hat", "low tom", "open hi-hat", "low-mid tom", "hi-mid tom",
            "crash cymbal 1", "high tom", "ride cymbal 1", "chinese cymbal", "ride bell", "tambourine", "splash cymbal",
            "cowbell", "crash cymbal 2", "vibraslap", "ride cymbal 2", "hi bongo", "low bongo", "mute hi conga",
            "open hi conga", "low conga", "high timbale", "low timbale", "high agogô", "low agogô", "cabasa", "maracas",
            "short whistle", "long whistle", "short güiro", "long güiro", "claves", "hi wood block", "low wood block",
            "mute cuíca", "open cuíca", "mute triangle", "open triangle" };
        return note >= 35 && note <= 81 ? names[note - 35] : std::string();
    }
}
