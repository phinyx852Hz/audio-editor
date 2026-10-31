// SPDX-License-Identifier: AGPL-3.0-only
#include "Compose.h"
#include "Fugue.h"
#include "Minimalize.h"
#include "Stochastic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <cstring>

namespace composition
{
    //==========================================================================
    const std::vector<MethodInfo>& methods()
    {
        static const std::vector<MethodInfo> all {
            { Method::organum, "organum", "Organum", "Notre Dame school (Paris, c. 1170–1230)", "europe",
              "A chant tenor held in long notes while the voices above it move in the rhythmic modes, consonant with it on each long; "
              "then a discant clausula, the tenor in the fifth mode too; Pérotin's voices exchange their phrases.",
              "Magnus liber organi; Anonymous IV; Johannes de Garlandia, De mensurabili musica (rhythmic modes); Roesner", "",
              3, 4, "tenor|duplum|triplum|quadruplum" },
            { Method::isorhythm, "isorhythm", "Isorhythm", "Ars nova (France, 14th c.)", "europe",
              "The tenor repeats a rhythm (talea) and a melody (color) of different lengths, so they meet again only after several "
              "statements; the second half in diminution. The upper voices repeat taleae of their own (panisorhythm), with hockets at the joins.",
              "Philippe de Vitry; Guillaume de Machaut's motets; Bent, \"Isorhythm\" (Grove)", "",
              3, 4, "tenor|motetus|triplum|contratenor" },
            { Method::counterpoint, "counterpoint", "Species Counterpoint", "Johann Joseph Fux (Vienna, 1725)", "europe",
              "A cantus firmus written by Fux's rules, then voices against it in each species in turn, from note against note to florid.",
              "Fux, Gradus ad Parnassum; Jeppesen, Counterpoint", "",
              3, 4, "cantus firmus|counterpoint 1|counterpoint 2|counterpoint 3" },
            { Method::fugue, "fugue", "Fugue", "Baroque (J. S. Bach and the textbook fugue)", "europe",
              "A subject written for it, then the exposition (subject and answer in each voice), episodes in sequence, middle entries "
              "and a stretto, closing on a tonic pedal.",
              "Gédalge, Traité de la fugue; Mann, The Study of Fugue", "",
              3, 4, "soprano|alto|tenor|bass" },
            { Method::stochastic, "stochastic", "Stochastic Matrix", "Iannis Xenakis, Achorripsis (1957)", "europe",
              "A matrix of timbres by sections; how many events each cell holds is drawn so the counts follow Poisson's law, "
              "and each cell is a cloud of notes by the laws of Formalized Music.",
              "Xenakis, Formalized Music, ch. I; Arsenault, \"Iannis Xenakis's Achorripsis: The Matrix Game\"", "",
              7, 8, "flutes|oboes|string glissandi|percussion|pizzicato|brass|arco strings|clarinets" },
            { Method::aleatory, "aleatory", "Chance and Aleatory", "John Cage (1951) and Witold Lutosławski (1961)", "europe",
              "Cage's chance operations: each sound, length and dynamic cast from charts of 64 by the I Ching, the sections in "
              "square-root proportions; then Lutosławski's ad libitum, every part looping its own figure out of time, ended by a cue.",
              "Cage, Music of Changes; Pritchett, The Music of John Cage; Lutosławski, Jeux vénitiens; Rae", "",
              4, 8, "piano|flute|clarinet|violin|cello|vibraphone|harp|percussion" },
            { Method::silence, "silence", "4′33″ (Silence)", "John Cage (1952)", "europe",
              "Three movements, all tacet, in the premiere's lengths (33″, 2′40″, 1′20″), the lid closed and opened for each; "
              "what sounds is the place, by chance: the room, the audience, the street, rain and wind, and the high and low sounds Cage heard in an anechoic chamber.",
              "Cage, 4′33″ (1952) and Silence (1961); Kahn, Noise Water Meat; Gann, No Such Thing as Silence", "",
              2, 8, "performer (the lid)|room|audience|street|rain|nervous system (high)|circulation (low)|wind" },
            { Method::starCharts, "starcharts", "Star Charts", "John Cage, Atlas Eclipticalis (1961–62)", "europe",
              "A page of a star atlas laid over each part's staff: across is time, up is pitch, each star a sound, louder and "
              "longer the brighter it is; stars in constellations on a scattered field, and a conductor's clock marking the pages.",
              "Cage, Atlas Eclipticalis; Pritchett, The Music of John Cage; Nicholls (ed.), The Cambridge Companion to John Cage", "",
              6, 8, "flute|clarinet|horn|trumpet|violin|viola|cello|harp" },
            { Method::musicircus, "musicircus", "Musicircus", "John Cage (Champaign-Urbana, 1967)", "europe",
              "Many musics at once and none of them coordinated: each part is one part of another Compose method, at a tempo of its "
              "own, coming in and going out by chance.",
              "Cage, Musicircus (1967) and HPSCHD (1969, with Hiller); Pritchett, The Music of John Cage", "",
              5, 8, "act 1|act 2|act 3|act 4|act 5|act 6|act 7|act 8" },
            { Method::process, "process", "Process Music", "Steve Reich and Philip Glass (1960s–70s)", "europe",
              "A cell written for it, then gradual processes on it: phasing and build-up (Reich), additive and subtractive "
              "process (Glass), and a pulse, each stage repeated.",
              "Reich, \"Music as a Gradual Process\" (1968); Potter, Four Musical Minimalists", "",
              3, 8, "figure|pattern (phasing)|pattern (held)|pulse|bass|counter-figure|cycle|arpeggio" },
            { Method::tintinnabuli, "tintinnabuli", "Tintinnabuli", "Arvo Pärt (Estonia, from 1976)", "europe",
              "A melodic voice moving by step around a centre, phrases growing by a note, and a voice on the tonic triad's nearest "
              "note to it; their mirror; a bell; and a mensuration canon, each voice slower and lower.",
              "Pärt, Für Alina, Spiegel im Spiegel, Cantus; Hillier, Arvo Pärt", "",
              3, 8, "melody (M)|triad (T)|mirror (M)|mirror (T)|bell|canon at half speed|canon at quarter speed|pedal" },
            { Method::spectral, "spectral", "Spectral", "Gérard Grisey and Tristan Murail (France, 1970s)", "europe",
              "The harmonic series of a low fundamental as the harmony, each part a partial in its own cents; each cycle an attack and "
              "its resonance, breathing from harmonic to inharmonic (a stretched spectrum) and back.",
              "Grisey, Partiels; Murail, Gondwana; Fineberg, \"Guide to the Basic Concepts of Spectral Music\"", "",
              6, 8, "fundamental|partial 3|partial 5|partial 7|partial 2|partial 9|partial 11|partial 13" },
            { Method::tuningMeditation, "tuningmeditation", "Tuning Meditation", "Pauline Oliveros (first sung at the Guggenheim, 1981)", "europe",
              "As its score gives it, a breath at a time, each voice at its own pace: breathe out, listen for a tone, sing it; listen; "
              "tune to a distant voice; listen; sing a new tone no one is singing; then listen, and sing one's own tone or tune to another's, by turns.",
              "Oliveros, The Tuning Meditation (Deep Listening Publications, 2007); Oliveros, Deep Listening: A Composer's Sound Practice (2005)", "",
              4, 8, "voice 1 (soprano)|voice 2 (alto)|voice 3 (tenor)|voice 4 (bass)|voice 5|voice 6|voice 7|voice 8" },
            { Method::timeBrackets, "timebrackets", "Time Brackets", "John Cage, the Number Pieces (1987–92)", "europe",
              "Each part a row of time brackets: a window to start in and an overlapping one to stop in (0′00″↔0′45″, 0′30″↔1′15″), "
              "each holding one quiet sound or a few, cast by the I Ching from a small gamut; some brackets are empty.",
              "Cage, Four, Five, Seven, One11; Haskins, \"Time Brackets\"; Weisser, The Number Pieces of John Cage", "",
              4, 8, "flute|clarinet|percussion|piano|violin|viola|cello|trombone" },
            { Method::deepListening, "deeplistening", "Deep Listening", "Pauline Oliveros and the Deep Listening Band (1989)", "europe",
              "Sonic awareness: attention moving between the global (the circle: long tones that blend in the space, each one making, "
              "imagining, listening or remembering a sound) and the focal (the dot: one player's phrase over the tonal centre); "
              "the Expanded Instrument System's delays and a cistern's long ring.",
              "Oliveros, Dempster, Panaiotis, Deep Listening (1989, the Fort Worden cistern); Von Gunden, The Music of Pauline Oliveros; "
              "Oliveros, \"The Expanded Instrument System\"", "",
              4, 8, "accordion|trombone|voice|didjeridu|EIS (accordion's delays)|EIS (trombone's delays)|cistern (the ring)|conch" },
            { Method::raga, "raga", "Rāga Development", "Hindustani music (North India)", "south and west asia",
              "The rāga unfolded note by note from the lower Sa in ālāp (badhat), around its vādī and samvādī, rising to the upper "
              "Sa; then joṛ with a pulse, jhālā with the chikārī, and a gat in tīntāl ending in a tihāī on sam, over the tānpūrā.",
              "Jairazbhoy, The Rāgs of North Indian Music; Bor, The Raga Guide; Clayton, Time in Indian Music", "yaman (kalyan)",
              4, 7, "sitār|tānpūrā|tablā|chikārī|sāraṅgī|swarmaṇḍal|drone" },
            { Method::maqam, "maqam", "Maqām: Taqsīm and Waṣla", "Arab music (Egypt, the Levant)", "south and west asia",
              "A taqsīm through the maqām's ajnās, from the qarār up to the ghammāz and the jawāb, a modulation and a qafla home; "
              "then a waṣla: a samāʿī in four khānāt with its taslīm, a muwashshaḥ, and a closing qafla.",
              "Marcus, Music in Egypt; Racy, Making Music in the Arab World; Touma, The Music of the Arabs", "rast",
              4, 8, "ʿūd|darbukka|qānūn|nāy|riqq|kamān|voice|bass" },
            { Method::radif, "radif", "Radif and Dastgāh", "Persian classical music (Iran)", "south and west asia",
              "A dastgāh's gushe-hā in the order of the radif: the darāmad, then each gushe on a higher shāhed with its own ist, to "
              "the owj, and the forud back home; the voice's phrases answered by the tār; a chahārmezrāb and a reng to close.",
              "During, Mirabdolbaghi and Safvat, The Art of Persian Music; Nettl, The Radif of Persian Music; Farhat, The Dastgāh Concept",
              "shur", 4, 6, "tār|āvāz (voice)|tonbak|santur|kamāncheh|ney" },
            { Method::gamelan, "gamelan", "Gamelan Colotomic Form", "Javanese gamelan (Central Java)", "east and southeast asia",
              "A balungan in gatra of four, its last note the seleh, in a ladrang's gong cycle: kenong, kempul, kethuk and the gong "
              "marking its points; saron peking and bonang elaborate it, twice as dense in irama II; a buka to begin and a suwuk to end.",
              "Becker, Traditional Music in Modern Java; Sumarsam, Gamelan; Pickvance, A Gamelan Manual; Hood", "slendro pathet manyura",
              6, 8, "balungan|saron peking|bonang barung|kenong|gong & kempul|kethuk & kempyang|kendhang|gendèr" },
            { Method::joHaKyu, "johakyu", "Jo-ha-kyū", "Japanese gagaku and nō (Zeami)", "east and southeast asia",
              "Three parts: jo, a slow unmetred opening (the netori); ha, metred and quickening; kyū, fast to a sudden close; each "
              "part quickening within itself. The ensemble's layers: flute and hichiriki in heterophony, the shō's aitake clusters, koto, biwa and drums.",
              "Zeami, Kakyō; Malm, Traditional Japanese Music and Musical Instruments; Garfias, Music of a Thousand Autumns",
              "hyojo", 5, 8, "ryūteki|hichiriki|shō|kakko|koto|biwa|taiko|shōko" },
            { Method::sanjo, "sanjo", "Sanjo", "Korean folk music (Kim Chang-jo, 1890s)", "east and southeast asia",
              "A solo over the janggu through the jangdan from slow to fast: jinyangjo, jungmori, jungjungmori, jajinmori, hwimori, "
              "the phrases tied in tension and released (maeggo, pulgo). More instruments make a sanjo hapju, each its own version of the line.",
              "Howard, Korean Music: A Listening Guide; Lee Byong Won; Provine, \"Sanjo\" (Grove)", "gyemyeonjo",
              2, 8, "gayageum|janggu|geomungo|daegeum|haegeum|ajaeng|piri|buk" },
            { Method::chineseSuite, "chinese", "Sǎn-màn-zhōng-kuài-sǎn", "Chinese instrumental music", "east and southeast asia",
              "One tune (qupai) of four phrases, qǐ, chéng, zhuǎn, hé, each beginning on the note the last ended on (yú yǎo wěi), "
              "played free, slow, moderate and fast (banshi variation), and free again at the end.",
              "Witzleben, Silk and Bamboo Music in Shanghai; Lau, Music in China; Jones, Folk Music of China", "",
              5, 8, "erhu|pipa|guzheng|bangu|dizi|yangqin|zhongruan|luogu" },
            { Method::ostinato, "ewe", "Layered Ostinato", "Ewe drumming (Ghana and Togo)", "africa",
              "The bell's timeline of twelve pulses, then the rattle, kaganu, kidi and sogo enter one by one, each an ostinato placed "
              "against it, three against two; the lead drum's calls change the responses; a cantor and chorus sing.",
              "Locke, Drum Gahu and \"Africa/Ewe, Mande, Dagbamba, Shona, BaAka\" in Worlds of Music; Agawu, Representing African Music; Jones",
              "", 6, 8, "gankogui (bell)|axatse (rattle)|kaganu|kidi|sogo|atsimevu (lead)|cantor|chorus" },
            { Method::hocket, "hocket", "Hocket and Interlocking", "Central Africa (Banda-Linda horns, Aka)", "africa",
              "A cyclic melody shared out note by note: each player has one pitch, and their figures interlock to make it; then each "
              "figure varies within the cycle while the melody stays.",
              "Arom, African Polyphony and Polyrhythm (1991); Kubik", "",
              5, 8, "horn 1|horn 2|horn 3|horn 4|horn 5|horn 6|horn 7|horn 8" },
        };
        return all;
    }

    const MethodInfo& info (Method m)
    {
        for (const auto& x : methods())
            if (x.method == m)
                return x;
        return methods().front();
    }

    int find (const std::string& id)
    {
        const auto& all = methods();
        for (size_t i = 0; i < all.size(); ++i)
            if (id == all[i].id)
                return (int) i;
        return -1;
    }

    namespace
    {
        using stochastic::Random;
        constexpr int64_t Q = ticksPerQuarter;

        int below (Random& r, int n) { return n <= 1 ? 0 : std::min (n - 1, (int) (r.uniform() * n)); }
        bool chance (Random& r, double p) { return r.uniform() < p; }
        int between (Random& r, int lo, int hi) { return lo + below (r, hi - lo + 1); }
        int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

        std::vector<std::string> split (const char* names)
        {
            std::vector<std::string> out { std::string() };
            for (const char* c = names; *c != 0; ++c)
                if (*c == '|')
                    out.emplace_back();
                else
                    out.back() += *c;
            return out;
        }

        //======================================================================
        // The mode as degrees: degree 0 the tonic (key 60 + root), n() a period up, -1 the degree below.
        struct Scale
        {
            counterpoint::Mode m;
            std::vector<int> keys;   // the period's keys the mode uses
            std::vector<bool> up, down;   // by degree: may be reached going up, coming down (a rāga's āroha, avaroha)

            explicit Scale (counterpoint::Mode mode)
            {
                if (! mode.valid())
                {
                    mode = counterpoint::Mode::chromatic (0);
                    mode.scale.assign (12, false);
                    for (int k : { 0, 2, 4, 5, 7, 9, 11 })
                        mode.scale[(size_t) k] = true;
                }
                m = mode;
                for (int k = 0; k < m.period; ++k)
                    if (m.scale[(size_t) k])
                    {
                        keys.push_back (k);
                        up.push_back (m.up.empty() || m.up[(size_t) k]);
                        down.push_back (m.down.empty() || m.down[(size_t) k]);
                    }
            }

            int n() const { return (int) keys.size(); }
            int index (int d) const { return d - floorDiv (d, n()) * n(); }
            int key (int d) const
            {
                const auto k = 60 + m.root + floorDiv (d, n()) * m.period + keys[(size_t) index (d)];
                return std::clamp (k, 0, 127);
            }
            // Cents above the tonic, as the tuning plays it.
            double cents (int d) const { return floorDiv (d, n()) * 1200.0 + m.keyCents[(size_t) keys[(size_t) index (d)]]; }
            // The degree nearest `c` cents above the tonic.
            int nearest (double c) const
            {
                int best = 0;
                for (int d = -4 * n(); d <= 4 * n(); ++d)
                    if (std::abs (cents (d) - c) < std::abs (cents (best) - c))
                        best = d;
                return best;
            }
            bool has (double c, double within = 45.0) const { return std::abs (cents (nearest (c)) - c) <= within; }
            // A step up or down that the mode allows that way.
            int stepUp (int d) const
            {
                for (int k = 1; k <= n(); ++k)
                    if (up[(size_t) index (d + k)])
                        return d + k;
                return d + 1;
            }
            int stepDown (int d) const
            {
                for (int k = 1; k <= n(); ++k)
                    if (down[(size_t) index (d - k)])
                        return d - k;
                return d - 1;
            }
            // The tonic triad's degrees in one period: the tonic, the third nearest a third, the fifth.
            std::vector<int> triad() const
            {
                std::vector<int> t { 0 };
                const auto third = std::abs (cents (nearest (316.0)) - 316.0) < std::abs (cents (nearest (386.0)) - 386.0) ? nearest (316.0) : nearest (386.0);
                if (third > 0 && third < n())
                    t.push_back (third);
                const auto fifth = nearest (702.0);
                if (fifth > t.back() && fifth < n())
                    t.push_back (fifth);
                return t;
            }
            int fifth() const { return has (702.0) ? nearest (702.0) : std::max (1, n() / 2); }
            int fourth() const { return has (498.0) ? nearest (498.0) : std::max (1, n() / 2 - 1); }
        };

        // How consonant an interval is, in cents: 0 perfect (unison, octave, fifth), 1 imperfect
        // (thirds, sixths, and the fourth when `fourth`), 3 dissonant.
        int consonance (double cents, bool fourth)
        {
            const auto c = std::fmod (std::abs (cents), 1200.0);
            auto near = [c] (double x, double w) { return std::abs (c - x) <= w || std::abs (c - x - 1200.0) <= w; };
            if (near (0.0, 35.0) || near (1200.0, 35.0) || near (702.0, 30.0))
                return 0;
            if (near (498.0, 30.0))
                return fourth ? 0 : 2;
            if ((c >= 250.0 && c <= 420.0) || (c >= 770.0 && c <= 930.0))
                return 1;
            return 3;
        }

        // A degree in [lo, hi] near `near` that sounds well against `against`: perfect only, or
        // any consonance; the nearer the better, a little chance among equals.
        int consonant (const Scale& sc, int against, int near, int lo, int hi, bool perfect, bool fourth, Random& r)
        {
            int best = std::clamp (near, lo, hi);
            double bestCost = 1e9;
            for (int d = lo; d <= hi; ++d)
            {
                const auto c = consonance (sc.cents (d) - sc.cents (against), fourth);
                if (c >= 3 || (perfect && c > 0))
                    continue;
                const auto cost = std::abs (d - near) + (perfect ? 0 : c * 0.5) + r.uniform() * 0.9;
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = d;
                }
            }
            return best;
        }

        // The next degree of a line: mostly a step, now and then a leap that the next note turns
        // back by step, kept in [lo, hi]. `dir` is the last move (+, -, 0).
        int walk (const Scale& sc, int d, int lo, int hi, Random& r, int& dir, double leap = 0.2)
        {
            int next;
            if (std::abs (dir) >= 2)                    // after a leap, a step back
                next = dir > 0 ? sc.stepDown (d) : sc.stepUp (d);
            else if (chance (r, leap))
                next = d + (chance (r, 0.5) ? 1 : -1) * between (r, 2, 3);
            else if (chance (r, 0.12))
                next = d;
            else
                next = chance (r, dir == 0 ? 0.5 : dir > 0 ? 0.65 : 0.35) ? sc.stepUp (d) : sc.stepDown (d);
            if (next > hi)
                next = sc.stepDown (std::min (d, hi + 1));
            if (next < lo)
                next = sc.stepUp (std::max (d, lo - 1));
            next = std::clamp (next, lo, hi);
            dir = next - d;
            return next;
        }

        // A chant or cantus: from the final, by step, rising to a high point after the middle and
        // coming down to the final by step.
        std::vector<int> chant (const Scale& sc, int notes, Random& r, int lo = -2, int hi = -1)
        {
            if (hi < 0)
                hi = sc.n();
            notes = std::max (notes, 3);
            std::vector<int> out { 0 };
            const int peakAt = std::max (1, (int) std::lround (notes * (0.55 + 0.2 * r.uniform())));
            const int peak = std::clamp (between (r, sc.n() / 2, sc.n()), 2, hi);
            int dir = 0;
            for (int i = 1; i < notes - 2; ++i)
            {
                auto d = out.back();
                const int target = i < peakAt ? peak : 1;
                int next = walk (sc, d, lo, hi, r, dir, 0.18);
                if (chance (r, 0.55))   // toward the high point, then the final
                    next = target > d ? sc.stepUp (d) : target < d ? sc.stepDown (d) : next;
                if (next == d)
                    next = target >= d ? sc.stepUp (d) : sc.stepDown (d);
                if (next >= peak && i != peakAt)   // one high point
                    next = sc.stepDown (d);
                next = std::clamp (next, lo, hi);
                dir = next - d;
                out.push_back (next);
            }
            // The last two by step: the second degree (or the seventh below), then the final.
            out.push_back (out.back() <= 0 ? -1 : 1);
            out.push_back (0);
            out.resize ((size_t) notes);
            out.back() = 0;
            return out;
        }

        void add (Part& p, int64_t start, int64_t length, int key, int velocity, double cents = 0.0)
        {
            if (length <= 0)
                return;
            p.notes.push_back ({ start, start + length, std::clamp (key, 0, 127), std::clamp (velocity, 1, 127), cents });
        }

        void mark (Result& out, int64_t at, const std::string& name)
        {
            if (! out.sections.empty() && out.sections.back().start == at)
                out.sections.back().name = name;
            else
                out.sections.push_back ({ at, name });
        }

        // General MIDI drums for the percussion parts.
        namespace gm
        {
            constexpr int kick = 36, acousticKick = 35, rim = 37, snare = 38, lowFloorTom = 41, lowTom = 45, lowMidTom = 47,
                          hiMidTom = 48, highTom = 50, chineseCymbal = 52, rideBell = 53, tambourine = 54, cowbell = 56,
                          muteHiConga = 62, openHiConga = 63, lowConga = 64, highTimbale = 65, lowTimbale = 66,
                          highAgogo = 67, lowAgogo = 68, cabasa = 69, maracas = 70, highWood = 76, lowWood = 77, shaker = 82;
        }

        void hit (Part& p, int64_t at, int drum, int velocity, int64_t length = Q / 4) { add (p, at, length, drum, velocity); }

        // A rhythm written as text, a character a step: the drum each letter plays (from `kit`), '.' a rest.
        // Upper case: accented.
        void pattern (Part& p, int64_t at, int64_t step, const std::string& text, const std::map<char, int>& kit, int velocity)
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                const auto c = text[i];
                const auto lower = (char) std::tolower ((unsigned char) c);
                if (const auto k = kit.find (lower); k != kit.end())
                    hit (p, at + (int64_t) i * step, k->second, c != lower ? std::min (127, velocity + 22) : velocity, step);
            }
        }

        Result begin (const Settings& s, int defaultParts = 0)
        {
            Result out;
            out.bpm = s.bpm;
            const auto& inf = info (s.method);
            for (const auto& name : split (inf.parts))
                out.parts.push_back ({ name, {}, false });
            (void) defaultParts;
            return out;
        }

        int64_t barTicks() { return 4 * Q; }
    }

    //==========================================================================
    counterpoint::Line cantusFirmus (const counterpoint::Mode& mode, int notes, uint32_t seed)
    {
        Scale sc (mode);
        Random r (seed);
        counterpoint::Line line;
        int64_t t = 0;
        for (auto d : chant (sc, std::clamp (notes, 4, 32), r, -2, sc.n()))
        {
            line.push_back ({ t, t + 4 * Q, sc.key (d), 80 });
            t += 4 * Q;
        }
        return line;
    }

    namespace
    {
        //======================================================================
        // Organum: Léonin's organum purum, Pérotin's voices over a held tenor, a discant clausula.
        // The rhythmic modes in breves (an eighth here); a long is two.
        const std::vector<std::vector<int>> rhythmicModes { { 2, 1 }, { 1, 2 }, { 3, 1, 2 }, { 2, 1, 3 }, { 3, 3 }, { 1, 1, 1 } };

        // An upper voice over the tenor `td` from `from` to `to`: ordines of the mode's feet, each
        // long consonant (unison, fourth, fifth, octave) with the tenor, the breves between by step.
        void upperVoice (Part& p, const Scale& sc, int td, int64_t from, int64_t to, int modeIndex, int lo, int hi, int& d, Random& r, int velocity)
        {
            const auto unit = Q / 2;
            const auto& foot = rhythmicModes[(size_t) modeIndex];
            int64_t t = from;
            int dir = 0;
            while (t < to)
            {
                const int feet = between (r, 2, 4);   // an ordo, then a rest of a breve
                for (int f = 0; f < feet && t < to; ++f)
                    for (size_t k = 0; k < foot.size() && t < to; ++k)
                    {
                        const auto len = std::min<int64_t> (foot[k] * unit, to - t);
                        if (k == 0)
                            d = consonant (sc, td, d + (chance (r, 0.5) ? 1 : -1), lo, hi, true, true, r);
                        else
                            d = walk (sc, d, lo, hi, r, dir, 0.1);
                        add (p, t, len, sc.key (d), velocity + (k == 0 ? 8 : 0));
                        t += foot[k] * unit;
                    }
                t += unit;
            }
        }

        Result organum (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int voices = std::clamp (s.parts, 1, 4);
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            const auto unit = Q / 2;
            const auto chantNotes = chant (sc, between (r, 9, 13), r, -2, sc.n() - 1);
            const auto tenorOf = [&] (int d) { return sc.key (d - sc.n()); };   // the tenor an octave down
            std::vector<int> pos (4, 0);
            const int lo = -sc.n(), hi = sc.n() / 2 + 1;

            // 1. Organum purum: the chant's first notes held, the upper voices in the first mode.
            const int held = 3;
            const auto purum = (int64_t) (total * 0.45) / held / (6 * unit) * (6 * unit);
            int64_t t = 0;
            mark (out, 0, "organum purum");
            for (int i = 0; i < held; ++i)
            {
                const auto td = chantNotes[(size_t) i] - sc.n();
                add (out.parts[0], t, purum, tenorOf (chantNotes[(size_t) i]), 72);
                // Pérotin's voice exchange: the duplum's phrase in the first half goes to the triplum in the second.
                const auto half = purum / 2 / (3 * unit) * (3 * unit);
                for (int v = 1; v < voices; ++v)
                {
                    auto seedPart = Random (s.seed * 31u + (uint32_t) (i * 7 + (v == 2 ? 1 : v)));
                    auto& rr = voices >= 3 && v <= 2 ? seedPart : r;
                    int d = pos[(size_t) v] != 0 ? pos[(size_t) v] : 0;
                    upperVoice (out.parts[(size_t) v], sc, td, t, t + half, 0, lo, hi, d, rr, 78);
                    pos[(size_t) v] = d;
                }
                if (voices >= 3)   // the exchange: the second half's phrases swapped between duplum and triplum
                    for (int v = 1; v <= 2; ++v)
                    {
                        auto rr = Random (s.seed * 31u + (uint32_t) (i * 7 + (v == 1 ? 1 : 2) + 3));
                        int d = pos[(size_t) v];
                        upperVoice (out.parts[(size_t) v], sc, td, t + half, t + purum, v == 1 ? 3 : 0, lo, hi, d, rr, 76);
                        pos[(size_t) v] = d;
                    }
                else
                    for (int v = 1; v < voices; ++v)
                    {
                        int d = pos[(size_t) v];
                        upperVoice (out.parts[(size_t) v], sc, td, t + half, t + purum, 0, lo, hi, d, r, 76);
                        pos[(size_t) v] = d;
                    }
                t += purum;
            }

            // 2. Discant clausula: the next chant notes twice (two cursus), the tenor in the fifth mode,
            // three longs and a rest; the voices note against note in the first.
            mark (out, t, "discant clausula");
            std::vector<int> segment (chantNotes.begin() + held, chantNotes.end() - 1);
            const auto closeAt = total - (int64_t) (total * 0.18) / (6 * unit) * (6 * unit);
            for (int cursus = 0; cursus < 2 && t < closeAt; ++cursus)
            {
                if (cursus == 1)
                    mark (out, t, "clausula, second cursus");
                for (size_t i = 0; i < segment.size() && t < closeAt; ++i)
                {
                    const auto td = segment[i] - sc.n();
                    add (out.parts[0], t, 3 * unit, tenorOf (segment[i]), 76);
                    for (int v = 1; v < voices; ++v)
                    {
                        int d = consonant (sc, td, pos[(size_t) v] + (v % 2 == 0 ? 1 : -1), lo, hi, i % 3 == 0, true, r);
                        add (out.parts[(size_t) v], t, 2 * unit, sc.key (d), 80);
                        int dir = 0;
                        const auto b = walk (sc, d, lo, hi, r, dir, 0.0);
                        add (out.parts[(size_t) v], t + 2 * unit, unit, sc.key (b), 70);
                        pos[(size_t) v] = b;
                    }
                    t += 3 * unit;
                    if (i % 3 == 2)
                        t += 3 * unit;   // the ordo's rest
                }
            }

            // 3. The close: the final held, a last melisma and the octave and fifth above it.
            t = std::max (t, closeAt);
            mark (out, t, "organum purum, close");
            const auto end = t + std::max<int64_t> (12 * unit, total - t);
            add (out.parts[0], t, end - t, tenorOf (0), 74);
            const std::array<double, 4> finals { 0.0, 1200.0, 702.0, 1200.0 };
            for (int v = 1; v < voices; ++v)
            {
                int d = pos[(size_t) v];
                const auto cadence = end - 6 * unit;
                upperVoice (out.parts[(size_t) v], sc, -sc.n(), t, cadence, 0, lo, hi, d, r, 76);
                add (out.parts[(size_t) v], cadence, end - cadence, sc.key (sc.nearest (finals[(size_t) v]) - sc.n()), 82);
            }
            out.end = end;
            out.parts.resize ((size_t) voices);
            out.summary = "Organum on a chant of " + std::to_string (chantNotes.size()) + " notes: organum purum, a discant clausula in two cursus, and the close";
            return out;
        }

        //======================================================================
        // Isorhythm: talea and color in the tenor, panisorhythmic upper voices with hockets.
        Result isorhythm (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int voices = std::clamp (s.parts, 1, 4);
            // The talea: t values (in breves, a half note here), rests among them; the color 3t/2
            // notes, so two colors fill three taleae.
            const int t = 2 * between (r, 2, 4);   // 4, 6 or 8 notes
            const int c = t * 3 / 2;
            std::vector<int64_t> talea;
            std::vector<bool> rest;
            for (int i = 0; i < t; ++i)
            {
                talea.push_back (Q * 2 * between (r, 1, 3));
                rest.push_back (false);
            }
            talea.push_back (Q * 2);   // the talea ends in a rest
            rest.push_back (true);
            const auto taleaLength = std::accumulate (talea.begin(), talea.end(), (int64_t) 0);
            const auto color = chant (sc, c, r, -2, sc.n() - 1);

            // Each upper voice's own talea (in smaller values), the same in every statement.
            std::vector<std::vector<int64_t>> upper (4);
            for (int v = 1; v < 4; ++v)
            {
                int64_t sum = 0;
                const std::array<int64_t, 5> values { Q / 2, Q, Q, Q * 3 / 2, Q * 2 };
                while (sum < taleaLength - 2 * Q)
                {
                    auto x = values[(size_t) below (r, (int) values.size())];
                    x = std::min (x, taleaLength - 2 * Q - sum);
                    upper[(size_t) v].push_back (x);
                    sum += x;
                }
            }

            // How many taleae: three to a pair of colors, then as many again in diminution (values halved),
            // so the piece is one and a half times the taleae in full values.
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            const int full = std::max (3, (int) std::lround (total / 1.5 / (double) taleaLength / 3.0) * 3);
            const int statements = 2 * full;
            int64_t at = 0;
            int ci = 0;
            std::vector<int> pos { 0, sc.n() / 2, sc.n(), -sc.n() / 2 };
            for (int k = 0; k < statements; ++k)
            {
                const bool diminution = k >= full;
                const auto scale = diminution ? 0.5 : 1.0;
                mark (out, at, "talea " + std::to_string (k + 1) + (diminution ? " (diminution)" : "") + (k % 3 == 0 ? ", color " + std::to_string (k / 3 * 2 + 1) : ""));
                // The tenor: the talea's values on the color's notes, round and round.
                int64_t x = at;
                std::vector<std::pair<int64_t, int>> tenorNotes;   // start, degree
                for (size_t i = 0; i < talea.size(); ++i)
                {
                    const auto len = (int64_t) (talea[i] * scale);
                    if (! rest[i])
                    {
                        const auto d = color[(size_t) (ci++ % c)] - sc.n();
                        add (out.parts[0], x, len, sc.key (d), 74);
                        tenorNotes.push_back ({ x, d });
                    }
                    x += len;
                }
                const auto end = x;
                auto tenorAt = [&] (int64_t when)
                {
                    int d = tenorNotes.empty() ? -sc.n() : tenorNotes.front().second;
                    for (const auto& [st, dd] : tenorNotes)
                        if (st <= when)
                            d = dd;
                    return d;
                };
                // The upper voices: their taleae again, pitches new; consonant with the tenor where a
                // tenor note starts, by step otherwise; the last bar a hocket between motetus and triplum.
                for (int v = 1; v < voices; ++v)
                {
                    const bool contra = v == 3;
                    const int lo = contra ? -sc.n() - 2 : -2, hi = contra ? 1 : sc.n() + 3;
                    int64_t y = at;
                    int dir = 0;
                    for (size_t i = 0; i < upper[(size_t) v].size(); ++i)
                    {
                        const auto len = (int64_t) (upper[(size_t) v][i] * scale);
                        const auto hocket = ! contra && y >= end - (int64_t) (2 * Q * scale) - Q;
                        const bool sounds = ! hocket || ((i + (size_t) v) % 2 == 0);
                        const bool strong = std::any_of (tenorNotes.begin(), tenorNotes.end(), [y] (const auto& n) { return n.first == y; }) || i == 0;
                        auto& d = pos[(size_t) v];
                        d = strong ? consonant (sc, tenorAt (y), d, lo, hi, i == 0, false, r) : walk (sc, d, lo, hi, r, dir, 0.15);
                        if (sounds)
                            add (out.parts[(size_t) v], y, hocket ? len / 2 : len, sc.key (d), strong ? 84 : 74);
                        y += len;
                    }
                }
                at = end;
            }
            // A final long: the tenor's final, the fifth and octave over it.
            mark (out, at, "final long");
            const std::array<double, 4> finals { -1200.0, 702.0, 1200.0, -498.0 };
            for (int v = 0; v < voices; ++v)
                add (out.parts[(size_t) v], at, 4 * Q, sc.key (v == 0 ? -sc.n() : sc.nearest (finals[(size_t) v])), 80);
            out.end = at + 4 * Q;
            out.parts.resize ((size_t) voices);
            out.summary = "Isorhythm: a talea of " + std::to_string (t) + " notes and a color of " + std::to_string (c) + ", " + std::to_string (statements)
                          + " taleae, the last " + std::to_string (statements - full) + " in diminution";
            return out;
        }

        //======================================================================
        // Species counterpoint on a cantus written for it, each species in turn.
        Result speciesStudy (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 4);
            const int notes = std::clamp (s.bars / 5, 8, 14);
            auto cf = cantusFirmus (sc.m, notes, s.seed);
            const auto length = (int64_t) notes * 4 * Q;
            const char* const names[] { "first species", "second species", "third species", "fourth species (suspensions)", "fifth species (florid)" };
            const int species = std::clamp ((int) (s.bars / notes), 1, 5);
            int bent = 0;
            for (int k = 0; k < species; ++k)
            {
                const auto at = (int64_t) k * (length + 4 * Q);
                mark (out, at, names[k]);
                for (const auto& n : cf)
                    add (out.parts[0], n.start + at, n.end - n.start, n.pitch, 76);
                if (parts < 2)
                    continue;
                counterpoint::Settings cs;
                cs.species = (counterpoint::Species) (k + 1);
                cs.voices = parts - 1;
                cs.placement = parts - 1 >= 2 ? counterpoint::Placement::around : (k % 2 == 0 ? counterpoint::Placement::above : counterpoint::Placement::below);
                cs.seed = s.seed + (uint32_t) k * 101u + (uint32_t) below (r, 1000);
                cs.mode = sc.m;
                const auto res = counterpoint::generate (cf, cs);
                bent += res.bent;
                for (size_t v = 0; v < res.voices.size() && v + 1 < out.parts.size(); ++v)
                    for (const auto& n : res.voices[v])
                        add (out.parts[v + 1], n.start + at, n.end - n.start, n.pitch, n.velocity);
            }
            out.end = (int64_t) species * (length + 4 * Q) - 4 * Q;
            out.parts.resize ((size_t) parts);
            out.summary = "A cantus firmus of " + std::to_string (notes) + " notes and " + std::to_string (parts - 1) + " voices against it in "
                          + std::to_string (species) + (species == 1 ? " species" : " species in turn")
                          + (bent > 0 ? ", " + std::to_string (bent) + " strict rules bent where nothing else fitted" : "");
            return out;
        }

        //======================================================================
        // A fugue on a subject written for it.
        Result fugueOnSubject (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int voices = std::clamp (s.parts, 2, 4);
            // The subject: two bars from the tonic or the fifth, a head of longer notes, a tail of quicker ones, home to a degree of the tonic triad.
            counterpoint::Line subject;
            int d = chance (r, 0.7) ? 0 : sc.fifth();
            int dir = 0;
            int64_t t = 0;
            const std::array<int64_t, 3> head { Q * 2, Q, Q };
            for (auto len : head)
            {
                subject.push_back ({ t, t + len, sc.key (d), 84 });
                t += len;
                d = walk (sc, d, -2, sc.n() + 2, r, dir, 0.45);
            }
            while (t < 8 * Q - Q)
            {
                const auto len = chance (r, 0.6) ? Q / 2 : Q;
                subject.push_back ({ t, t + len, sc.key (d), 80 });
                t += len;
                d = walk (sc, d, -2, sc.n() + 2, r, dir, 0.15);
            }
            const auto tri = sc.triad();
            d = tri[(size_t) below (r, (int) tri.size())];
            subject.push_back ({ t, 8 * Q, sc.key (d), 80 });

            fugue::Settings fs;
            fs.voices = voices;
            fs.mode = sc.m;
            fs.middleEntries = std::clamp (s.bars / 16, 1, 4);
            fs.inversion = chance (r, 0.4);
            fs.seed = s.seed;
            const auto res = fugue::generate (subject, (int) Q, fs);
            out.parts.clear();
            for (size_t v = 0; v < res.voices.size(); ++v)
            {
                Part p { v < res.names.size() ? res.names[v] : "voice " + std::to_string (v + 1), {}, false };
                for (const auto& n : res.voices[v])
                    add (p, n.start, n.end - n.start, n.pitch, n.velocity);
                out.parts.push_back (std::move (p));
            }
            mark (out, 0, "exposition");
            for (const auto& x : res.plan)
                if (x.what != "subject" && x.what != "answer" && x.what != "countersubject")
                    if (out.sections.empty() || x.start > out.sections.back().start)
                        mark (out, x.start, x.what);
            out.end = res.end;
            out.summary = "A fugue in " + std::to_string (voices) + " voices on a subject of " + std::to_string (subject.size()) + " notes"
                          + (res.strettoDelay > 0 ? ", with a stretto" : "");
            return out;
        }

        //======================================================================
        // Achorripsis: a matrix of timbres by cells, the events in each by Poisson's law.
        Result stochasticMatrix (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int rows = std::clamp (s.parts, 1, 8);
            const auto cellTicks = 2 * barTicks();
            const int cols = std::max (4, s.bars / 2);
            // Poisson's law with λ = 0.6, as Xenakis took it: how many cells hold 0, 1, 2, 3, 4 events.
            const double lambda = 0.6;
            const int cells = rows * cols;
            std::vector<int> levels;
            double p = std::exp (-lambda);
            for (int k = 0; k <= 4; ++k)
            {
                const auto count = (int) std::lround (cells * p);
                for (int i = 0; i < count && (int) levels.size() < cells; ++i)
                    levels.push_back (k);
                p *= lambda / (k + 1);
            }
            while ((int) levels.size() < cells)
                levels.push_back (0);
            for (size_t i = levels.size(); i > 1; --i)   // dealt over the matrix at random
                std::swap (levels[i - 1], levels[(size_t) below (r, (int) i)]);
            for (int row = 0; row < rows; ++row)   // every timbre plays somewhere: a silent row takes an event from the busiest
            {
                const auto first = levels.begin() + row * cols;
                if (std::all_of (first, first + cols, [] (int l) { return l == 0; }))
                {
                    const auto most = std::max_element (levels.begin(), levels.end());
                    *(first + below (r, cols)) = 1;
                    if (*most > 1)
                        --*most;
                }
            }
            const std::array<double, 5> density { 0.0, 1.0, 2.0, 3.0, 5.0 };   // notes a beat
            // Each timbre's register (MIDI keys) and the law of its lengths.
            const std::array<std::pair<int, int>, 8> ranges { std::pair { 72, 96 }, { 60, 84 }, { 52, 88 }, { 0, 0 }, { 40, 79 }, { 40, 72 }, { 36, 84 }, { 50, 89 } };
            for (int row = 0; row < rows; ++row)
            {
                auto& part = out.parts[(size_t) row];
                part.drums = row == 3;
                for (int col = 0; col < cols; ++col)
                {
                    const auto level = levels[(size_t) (row * cols + col)];
                    if (level == 0)
                        continue;
                    stochastic::Settings ss;
                    ss.ticksPerQuarter = (int) Q;
                    ss.length = cellTicks;
                    ss.density = density[(size_t) level];
                    ss.sectionBeats = 8.0;
                    ss.low = part.drums ? 41 : ranges[(size_t) row].first;
                    ss.high = part.drums ? 50 : ranges[(size_t) row].second;
                    ss.lengthBeats = row == 4 || part.drums ? 0.25 : row == 6 ? 2.0 : 0.6;
                    ss.seed = s.seed * 977u + (uint32_t) (row * cols + col);
                    const auto res = stochastic::generate (ss);
                    for (const auto& n : res.notes)
                    {
                        auto key = n.pitch;
                        if (! part.drums)   // onto the mode's nearest degree
                            key = sc.key (sc.nearest ((key - 60 - sc.m.root) * 100.0));
                        else
                            key = std::array<int, 6> { gm::lowFloorTom, gm::lowTom, gm::hiMidTom, gm::highTom, gm::highWood, gm::lowWood }[(size_t) (n.pitch % 6)];
                        add (part, n.start + (int64_t) col * cellTicks, std::min (n.end - n.start, cellTicks), key, n.velocity);
                    }
                }
            }
            for (int col = 0; col < cols; col += 4)
                mark (out, (int64_t) col * cellTicks, "cells " + std::to_string (col + 1) + "–" + std::to_string (std::min (cols, col + 4)));
            out.end = (int64_t) cols * cellTicks;
            out.parts.resize ((size_t) rows);
            std::array<int, 5> counts {};
            for (auto l : levels)
                ++counts[(size_t) l];
            out.summary = "A matrix of " + std::to_string (rows) + " timbres by " + std::to_string (cols) + " cells; by Poisson's law (λ 0.6) "
                          + std::to_string (counts[0]) + " are silent, " + std::to_string (counts[1]) + " hold one event, " + std::to_string (counts[2])
                          + " two, " + std::to_string (counts[3]) + " three and " + std::to_string (counts[4]) + " four";
            return out;
        }

        //======================================================================
        // Chance: Cage's I Ching charts, then Lutosławski's ad libitum and a cue.
        // A hexagram: six lines, each three coins (heads 3, tails 2): 6 and 8 broken, 7 and 9 whole;
        // 6 and 9 are moving lines. Its number here is the six lines read as bits, 0..63.
        int hexagram (Random& r, bool& moving)
        {
            int n = 0;
            moving = false;
            for (int line = 0; line < 6; ++line)
            {
                int sum = 0;
                for (int coin = 0; coin < 3; ++coin)
                    sum += chance (r, 0.5) ? 3 : 2;
                n = n << 1 | (sum % 2);   // 7 and 9 (whole) are odd
                moving = moving || sum == 6 || sum == 9;
            }
            return n;
        }

        Result aleatory (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            out.parts[7].drums = true;
            // Charts of 64, as Cage's: sounds (half of them silences; a sound one to three notes
            // anywhere in the range), lengths and dynamics.
            struct Sound { std::vector<int> degrees; };
            auto newSound = [&] (int entry)
            {
                Sound x;
                if (entry % 2 == 1)
                    return x;   // a silence
                const int count = between (r, 1, 3);
                for (int i = 0; i < count; ++i)
                    x.degrees.push_back (between (r, -2 * sc.n(), 2 * sc.n()));
                return x;
            };
            std::array<Sound, 64> sounds;
            std::array<int64_t, 64> lengths;
            std::array<int, 64> dynamics;
            const std::array<int64_t, 8> values { Q / 4, Q / 3, Q / 2, Q * 2 / 3, Q, Q * 3 / 2, Q * 2, Q * 5 / 2 };
            for (int i = 0; i < 64; ++i)
            {
                sounds[(size_t) i] = newSound (i);
                lengths[(size_t) i] = values[(size_t) below (r, 8)] * (chance (r, 0.2) ? 2 : 1);
                dynamics[(size_t) i] = 20 + 107 * below (r, 16) / 15;   // ppp..fff in sixteen
            }
            // The structure: square-root form, proportions 3 5 6¾ 6¾ 5 3⅛ (Music of Changes), over
            // two thirds of the piece; the rest ad libitum and the cue.
            const auto total = std::max<int64_t> (s.bars, 12) * barTicks();
            const std::array<double, 6> proportions { 3.0, 5.0, 6.75, 6.75, 5.0, 3.125 };
            const auto sum = std::accumulate (proportions.begin(), proportions.end(), 0.0);
            const auto chancePart = total * 2 / 3;
            int64_t at = 0;
            for (size_t k = 0; k < proportions.size(); ++k)
            {
                const auto length = (int64_t) (chancePart * proportions[k] / sum) / Q * Q;
                // How many layers sound at once: cast too.
                bool moving = false;
                const int layers = 1 + hexagram (r, moving) % parts;
                mark (out, at, "chance, part " + std::to_string (k + 1) + " (" + std::to_string (layers) + (layers == 1 ? " layer)" : " layers)"));
                for (int layer = 0; layer < layers; ++layer)
                {
                    const int who = (layer + (int) k) % parts;
                    auto& part = out.parts[(size_t) who];
                    int64_t t = at;
                    while (t < at + length)
                    {
                        bool mv = false;
                        const auto hs = hexagram (r, mv);
                        const auto hl = hexagram (r, moving);
                        const auto hd = hexagram (r, moving);
                        const auto len = std::min (lengths[(size_t) hl], at + length - t);
                        const auto& sound = sounds[(size_t) hs];
                        for (auto d : sound.degrees)
                            add (part, t, len, part.drums ? std::array<int, 4> { gm::lowWood, gm::highWood, gm::lowTom, gm::highTom }[(size_t) (std::abs (d) % 4)]
                                                          : sc.key (who == 4 ? d - sc.n() : d), dynamics[(size_t) hd]);
                        if (mv)   // a moving hexagram: that sound is used up and another takes its place
                            sounds[(size_t) hs] = newSound (hs);
                        t += len;
                    }
                }
                at += length;
            }
            // Ad libitum: each part loops a figure of its own, from its share of one chord, at a tempo of
            // its own, not together, until the cue.
            mark (out, at, "ad libitum (aleatory counterpoint)");
            const auto cue = total - 2 * barTicks();
            for (int v = 0; v < parts; ++v)
            {
                auto& part = out.parts[(size_t) v];
                std::vector<int> figure;
                const int notes = between (r, 3, 6);
                int d = v * sc.n() / 3 - sc.n() + between (r, 0, sc.n() - 1);
                int dir = 0;
                for (int i = 0; i < notes; ++i)
                {
                    figure.push_back (d);
                    d = walk (sc, d, d - 4, d + 4, r, dir, 0.4);
                }
                const auto step = (int64_t) (Q / 2 * (0.7 + 0.6 * r.uniform()));
                int64_t t = at + (int64_t) (r.uniform() * Q);
                for (size_t i = 0; t < cue; ++i)
                {
                    const auto len = (int64_t) (step * (0.85 + 0.3 * r.uniform()));
                    add (part, t, len, part.drums ? gm::highWood : sc.key (figure[i % figure.size()]), 60 + below (r, 30));
                    t += len + (i % figure.size() == figure.size() - 1 ? step : 0);
                }
            }
            // The cue: a battuta, all together, the chord struck and held.
            mark (out, cue, "a battuta (the cue)");
            for (int v = 0; v < parts; ++v)
                add (out.parts[(size_t) v], cue, 2 * barTicks(), out.parts[(size_t) v].drums ? gm::lowTom : sc.key (v * 2 - sc.n()), 110);
            out.end = cue + 2 * barTicks();
            out.parts.resize ((size_t) parts);
            out.summary = "Chance operations by the I Ching in six parts of square-root proportions, then ad libitum to a cue, for "
                          + std::to_string (parts) + (parts == 1 ? " part" : " parts");
            return out;
        }

        //======================================================================
        // Cage after Music of Changes. A time in seconds at the score's tempo, and a clock time as
        // Cage's scores write it (1′30″).
        int64_t seconds (const Settings& s, double sec) { return (int64_t) std::llround (sec * s.bpm / 60.0 * Q); }
        std::string clock (const Settings& s, int64_t ticks)
        {
            const auto sec = (int) std::lround ((double) ticks / Q * 60.0 / std::max (1.0, s.bpm));
            auto two = [] (int x) { return (x < 10 ? "0" : "") + std::to_string (x); };
            return std::to_string (sec / 60) + "′" + two (sec % 60) + "″";
        }

        // 4′33″ (1952): three movements, each tacet, in the premiere's lengths (David Tudor,
        // Woodstock, 1952: 33″, 2′40″, 1′20″), which the piece is named for. Tudor closed the
        // keyboard's lid to begin each movement and opened it to end it. What sounds is the
        // place: sounds that come and go by chance (Poisson arrivals, each layer at its own rate),
        // all quiet, and the two sounds Cage heard in the anechoic chamber (1951), one high (his
        // nervous system) and one low (his blood circulating).
        Result silence (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const std::array<double, 3> movement { 33.0, 160.0, 80.0 };
            const std::array<const char*, 3> name { "I (33″)", "II (2′40″)", "III (1′20″)" };
            // The layers: how often each sounds (a second between, on average), and what with.
            struct Layer { double every; std::vector<int> sounds; int lo, hi; };
            const std::array<Layer, 8> layers {
                Layer { 0.0, { gm::lowWood }, 0, 0 },                                               // the lid
                Layer { 7.0, { gm::rim, gm::cabasa, gm::lowWood, gm::shaker }, 14, 34 },            // the room: a creak, a chair
                Layer { 5.0, { gm::maracas, gm::rim, gm::shaker, gm::tambourine }, 12, 40 },        // the audience: a cough, a programme
                Layer { 11.0, { gm::lowFloorTom, gm::kick, gm::chineseCymbal, gm::rideBell }, 10, 30 },   // the street
                Layer { 0.6, { gm::shaker, gm::cabasa, gm::maracas }, 8, 22 },                      // rain on the roof (Woodstock's second movement)
                Layer { 0.0, {}, 6, 14 },                                                           // the nervous system, high
                Layer { 0.0, {}, 6, 14 },                                                           // the circulation, low
                Layer { 9.0, { gm::cabasa, gm::shaker }, 8, 20 },                                   // wind in the trees (the first movement)
            };
            int64_t at = 0;
            for (size_t m = 0; m < movement.size(); ++m)
            {
                const auto length = seconds (s, movement[m]);
                mark (out, at, std::string ("movement ") + name[m] + ", tacet");
                for (int v = 0; v < parts; ++v)
                {
                    auto& part = out.parts[(size_t) v];
                    const auto& layer = layers[(size_t) v];
                    if (v == 0)   // the lid closed, then opened
                    {
                        add (part, at, Q / 4, gm::lowWood, 24);
                        add (part, at + length - Q / 4, Q / 4, gm::highWood, 18);
                        continue;
                    }
                    if (v == 5 || v == 6)   // a long tone, near the edge of hearing, that comes and goes
                    {
                        const int d = v == 5 ? 3 * sc.n() + between (r, 0, sc.n() - 1) : -3 * sc.n();
                        int64_t t = at + seconds (s, 2.0 + 6.0 * r.uniform());
                        while (t < at + length - seconds (s, 2.0))
                        {
                            const auto len = std::min (seconds (s, 6.0 + 14.0 * r.uniform()), at + length - t);
                            add (part, t, len, sc.key (d), between (r, layer.lo, layer.hi));
                            t += len + seconds (s, 4.0 + 10.0 * r.uniform());
                        }
                        continue;
                    }
                    // Rain in the second movement, wind in the first, the rest throughout.
                    if ((v == 4 && m != 1) || (v == 7 && m != 0))
                        continue;
                    int64_t t = at;
                    while (true)
                    {
                        t += std::max<int64_t> (1, seconds (s, -std::log (1.0 - r.uniform()) * layer.every));
                        if (t >= at + length)
                            break;
                        hit (part, t, layer.sounds[(size_t) below (r, (int) layer.sounds.size())], between (r, layer.lo, layer.hi), Q / 4);
                    }
                }
                at += length;
            }
            for (int v = 0; v < parts; ++v)
                out.parts[(size_t) v].drums = v != 5 && v != 6;
            out.end = at;
            out.parts.resize ((size_t) parts);
            out.summary = "4′33″ in three movements of 33″, 2′40″ and 1′20″ (at " + std::to_string ((int) s.bpm)
                          + " bpm), each tacet: only the place sounds, by chance, in " + std::to_string (parts) + (parts == 1 ? " layer" : " layers");
            return out;
        }

        // Atlas Eclipticalis (1961–62): star charts laid over staves. Each part has a page of its own
        // atlas: across it is time, up it is pitch, each star a sound, and the brighter the star the
        // louder and the longer. Stars lie in clusters (constellations) on a scattered field, and the
        // faint ones far outnumber the bright (about 2.5 times as many each magnitude). A conductor's
        // arms, a clock, mark each page's minutes; a player may leave a sound out.
        Result starCharts (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            const int pages = std::clamp ((int) (total / (8 * barTicks())), 1, 8);
            const auto page = total / pages;
            // Each part's range, in degrees: low instruments low.
            const std::array<std::pair<int, int>, 8> range { { { 0, 2 }, { -1, 1 }, { -2, 0 }, { 0, 2 }, { -1, 1 }, { -2, 0 }, { -3, -1 }, { -2, 2 } } };
            auto magnitude = [&r]
            {
                // 1 (brightest) .. 6: weights 1, 2.5, 6.25 ...
                const double pick = r.uniform() * (1 + 2.5 + 6.25 + 15.625 + 39.0625 + 97.65625);
                double acc = 0;
                for (int m = 1; m <= 6; ++m)
                {
                    acc += std::pow (2.5, m - 1);
                    if (pick < acc)
                        return m;
                }
                return 6;
            };
            for (int p = 0; p < pages; ++p)
                mark (out, p * page, "page " + std::to_string (p + 1) + " (the conductor's clock at " + clock (s, p * page) + ")");
            for (int v = 0; v < parts; ++v)
            {
                auto& part = out.parts[(size_t) v];
                const auto lo = range[(size_t) v].first * sc.n(), hi = (range[(size_t) v].second + 1) * sc.n() - 1;
                struct Star { double x, y; int mag; };
                std::vector<Star> stars;
                for (int p = 0; p < pages; ++p)
                {
                    // A few constellations, and the field between them.
                    const int clusters = between (r, 1, 3);
                    for (int c = 0; c < clusters; ++c)
                    {
                        const double cx = p + r.uniform(), cy = r.uniform();
                        const int count = between (r, 3, 8);
                        for (int k = 0; k < count; ++k)
                            stars.push_back ({ std::clamp (cx + (r.uniform() - 0.5) * 0.18, (double) p, p + 0.999),
                                               std::clamp (cy + (r.uniform() - 0.5) * 0.3, 0.0, 1.0), magnitude() });
                    }
                    const int field = between (r, 4, 10);
                    for (int k = 0; k < field; ++k)
                        stars.push_back ({ p + r.uniform(), r.uniform(), magnitude() });
                }
                std::sort (stars.begin(), stars.end(), [] (const Star& a, const Star& b) { return a.x < b.x; });
                for (size_t i = 0; i < stars.size(); ++i)
                {
                    const auto& st = stars[i];
                    if (st.mag >= 5 && chance (r, 0.3))   // the player leaves out a faint one
                        continue;
                    const auto t = (int64_t) (st.x * page);
                    const auto next = i + 1 < stars.size() ? (int64_t) (stars[i + 1].x * page) : total;
                    const auto len = std::clamp<int64_t> ((int64_t) (Q * (7 - st.mag) / 2), Q / 4, std::max<int64_t> (Q / 4, next - t + Q));
                    const int d = lo + (int) std::lround (st.y * (hi - lo));
                    add (part, t, std::min (len, total - t), sc.key (d), 118 - (st.mag - 1) * 18);
                }
                if (part.notes.empty())
                    add (part, 0, Q, sc.key (lo), 64);
            }
            out.end = total;
            out.parts.resize ((size_t) parts);
            out.summary = "Star charts: " + std::to_string (pages) + (pages == 1 ? " page" : " pages")
                          + " of an atlas a part, each star a sound, louder and longer the brighter it is, for " + std::to_string (parts)
                          + (parts == 1 ? " part" : " parts");
            return out;
        }

        // Musicircus (1967): many musics at once, none of them coordinated. Each act here is one part
        // of another Compose method, played at its own tempo; it comes in and goes out by chance.
        // Only the methods in the Mode menu's mode (no tradition's own tuning, no spectral cents) join.
        Result musicircus (const Settings& s, Random& r)
        {
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            std::vector<Method> acts;
            for (const auto& m : methods())
                if (*m.home == 0 && m.method != Method::spectral && m.method != Method::musicircus && m.method != Method::silence)
                    acts.push_back (m.method);
            // Shuffled, so each act is a different music while there are enough.
            for (size_t i = acts.size(); i > 1; --i)
                std::swap (acts[i - 1], acts[(size_t) below (r, (int) i)]);
            struct Entry { int64_t at; std::string text; };
            std::vector<Entry> entries;
            for (int v = 0; v < parts; ++v)
            {
                auto& part = out.parts[(size_t) v];
                const auto method = acts[(size_t) v % acts.size()];
                const auto& inf = info (method);
                Settings x = s;
                x.method = method;
                x.parts = inf.defaultParts;
                x.bars = s.bars;
                x.bpm = s.bpm * (0.6 + 0.9 * r.uniform());   // its own tempo
                x.seed = s.seed * 7919u + (uint32_t) v * 104729u + 1;
                const auto act = generate (x);
                // One of its parts, not an empty one.
                std::vector<size_t> full;
                for (size_t k = 0; k < act.parts.size(); ++k)
                    if (! act.parts[k].notes.empty())
                        full.push_back (k);
                if (full.empty())
                    continue;
                const auto& from = act.parts[full[(size_t) below (r, (int) full.size())]];
                const double stretch = s.bpm / x.bpm;   // its ticks at our tempo
                part.name = std::string (inf.name) + " (" + from.name + ")";
                part.drums = from.drums;
                // In and out: one to three stays, each a time bracket of its own.
                const int stays = between (r, 1, 3);
                int64_t t = (int64_t) (r.uniform() * total / (stays + 1)) / Q * Q;
                for (int k = 0; k < stays && t < total; ++k)
                {
                    const auto room = total - t;
                    const auto len = std::max<int64_t> (4 * barTicks(), (int64_t) (room * (0.35 + 0.5 * r.uniform()) / (stays - k)));
                    const auto until = std::min (total, t + len);
                    // Where in its own music it is: it went on playing while out of earshot.
                    const auto offset = (int64_t) (t / stretch);
                    for (const auto& n : from.notes)
                    {
                        const auto a = t + (int64_t) ((n.start - offset) * stretch);
                        const auto b = t + (int64_t) ((n.end - offset) * stretch);
                        if (n.start < offset || a >= until)
                            continue;
                        add (part, a, std::min (b, until) - a, n.key, n.velocity, n.cents);
                    }
                    entries.push_back ({ t, part.name + " comes in" });
                    entries.push_back ({ until, part.name + " goes out" });
                    t = until + (int64_t) (r.uniform() * barTicks() * 6) / Q * Q;
                }
                if (part.notes.empty())   // came in where its music had ended: from its beginning
                    for (const auto& n : from.notes)
                    {
                        const auto a = (int64_t) (n.start * stretch);
                        if (a >= total)
                            break;
                        add (part, a, std::min ((int64_t) ((n.end - n.start) * stretch), total - a), n.key, n.velocity, n.cents);
                    }
            }
            std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.at < b.at; });
            mark (out, 0, "the circus opens");
            for (const auto& e : entries)
                if (e.at < total)
                {
                    if (! out.sections.empty() && out.sections.back().start == e.at)
                        out.sections.back().name += "; " + e.text;
                    else
                        mark (out, e.at, e.text);
                }
            out.end = total;
            out.parts.resize ((size_t) parts);
            out.summary = "Musicircus: " + std::to_string (parts) + (parts == 1 ? " act" : " acts")
                          + " from other methods, each at its own tempo, in and out by chance";
            return out;
        }

        // The Number Pieces (1987–92: Two, Four, Five, Seven, One11 ...): each part a row of time
        // brackets. A bracket gives a window to start in and an overlapping window to stop in
        // (0′00″↔0′45″, 0′30″↔1′15″); its sound, one held tone or a few, is chosen by chance from the
        // piece's small gamut, mostly quiet. Some brackets are empty. The player decides when, within
        // the windows; here chance decides.
        Result timeBrackets (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            out.parts[2].drums = true;
            // The window and the bracket's length: Cage's 45″ and 30″, smaller when the piece is short.
            const auto window = std::min (seconds (s, 45.0), total / 6);
            const auto overlap = window * 2 / 3;
            // The gamut: a handful of the mode's degrees over two periods and a half, cast by the I Ching.
            std::vector<int> gamut;
            const int size = std::min (between (r, 5, 9), 5 * sc.n() / 2);
            while ((int) gamut.size() < size)
            {
                bool mv = false;
                const int d = hexagram (r, mv) % std::max (1, 5 * sc.n() / 2) - sc.n();
                if (std::find (gamut.begin(), gamut.end(), d) == gamut.end())
                    gamut.push_back (d);
            }
            std::sort (gamut.begin(), gamut.end());
            const std::array<int, 4> metal { gm::chineseCymbal, gm::rideBell, gm::lowTom, gm::tambourine };
            for (int64_t m = 0; m < total; m += window + overlap)
                mark (out, m, "time brackets from " + clock (s, m));
            int brackets = 0;
            for (int v = 0; v < parts; ++v)
            {
                auto& part = out.parts[(size_t) v];
                const auto low = v == 6 || v == 7 ? -sc.n() : 0;   // the cello and trombone an octave down
                // A part's brackets start at staggered times, so the parts do not change together.
                int64_t a = (int64_t) (r.uniform() * overlap);
                while (a + window + overlap <= total)
                {
                    ++brackets;
                    if (! chance (r, 0.22))   // else an empty bracket: silence
                    {
                        const auto start = a + (int64_t) (r.uniform() * window);
                        const auto stop = std::max (start + Q, a + overlap + (int64_t) (r.uniform() * window));
                        bool mv = false;
                        const int count = chance (r, 0.7) ? 1 : between (r, 2, 3);
                        const auto each = (stop - start) / count;
                        const int velocity = 34 + 6 * below (r, 8);   // pp to mf
                        for (int k = 0; k < count; ++k)
                        {
                            if (part.drums)
                                add (part, start + k * each, each, metal[(size_t) below (r, 4)], velocity);
                            else
                                add (part, start + k * each, each, sc.key (gamut[(size_t) (hexagram (r, mv) % (int) gamut.size())] + low), velocity);
                        }
                    }
                    a += window + overlap - (int64_t) (r.uniform() * overlap / 2);
                }
                if (part.notes.empty())
                    add (part, 0, window, part.drums ? gm::rideBell : sc.key (gamut.front() + low), 40);
            }
            out.end = total;
            out.parts.resize ((size_t) parts);
            out.summary = "Time brackets of " + clock (s, window) + " (to start) and " + clock (s, window) + " (to stop, " + clock (s, overlap)
                          + " later) in " + std::to_string (parts) + (parts == 1 ? " part" : " parts") + ", " + std::to_string (brackets)
                          + " brackets on a gamut of " + std::to_string (gamut.size()) + " pitches";
            return out;
        }

        //======================================================================
        // Pauline Oliveros. The Tuning Meditation (first sung by 75 singers under William Duckworth
        // at the Guggenheim, 1981), followed as its score gives it, a breath at a time, each singer at
        // their own pace: breathe out with air sound; listen with the mind's ear for a tone; sing it,
        // on one breath; listen to the whole field; tune as exactly as possible to a distant voice;
        // listen again; sing a new tone no one else is singing; then go on, listening and then
        // singing a tone of one's own or tuning to another's, by turns. One tone a breath, a new one
        // on the next; tune to distant partners and to as many voices as possible; a new tone
        // sounded so that it is heard far off; end when everyone else does.
        Result tuningMeditation (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            const auto end = total - seconds (s, 2.0) > total / 2 ? total - seconds (s, 2.0) : total;   // everyone ends together
            // Each voice's own register, soprano to bass, twice over.
            const std::array<int, 8> centre { sc.n(), sc.n() / 2, 0, -sc.n() / 2, sc.n() * 3 / 4, sc.n() / 4, -sc.n() / 4, -sc.n() };
            enum Step { air, inner, own, field, tune, again, fresh, go };
            struct Voice { int64_t next = 0; int step = air; int degree = 0; int64_t until = 0; bool tuneNext = true; std::vector<int> partners; };
            std::vector<Voice> voices ((size_t) parts);
            for (auto& v : voices)
                v.next = (int64_t) (r.uniform() * seconds (s, 3.0));
            int counts[3] = { 0, 0, 0 };   // own, tuned, new
            const std::array<const char*, 8> stepName { "breathing out, with air sound", "listening for a tone with the mind's ear",
                                                         "singing the tone heard", "listening to the whole field",
                                                         "tuning to a distant voice", "listening to the whole field again",
                                                         "a new tone no one is singing", "listening, then one's own tone or another's, by turns" };
            int marked = -1;
            while (true)
            {
                size_t who = 0;
                for (size_t k = 1; k < voices.size(); ++k)
                    if (voices[k].next < voices[who].next)
                        who = k;
                auto& v = voices[who];
                const auto t = v.next;
                if (t >= end - seconds (s, 2.0))
                    break;
                // The ensemble's stages, marked as the first voice reaches each.
                if (v.step > marked && v.step <= go)
                {
                    marked = v.step;
                    mark (out, out.sections.empty() ? 0 : std::max (out.sections.back().start + 1, t), stepName[(size_t) v.step]);
                }
                const auto breath = seconds (s, 4.0 + 6.0 * r.uniform());
                const auto len = std::min (breath - seconds (s, 0.8), end - t);
                // What the others sing now.
                std::vector<size_t> singing;
                for (size_t k = 0; k < voices.size(); ++k)
                    if (k != who && voices[k].until > t)
                        singing.push_back (k);
                auto pickTone = [&] (bool unsung)
                {
                    int d = centre[who] + between (r, -2, 3);
                    for (int tries = 0; unsung && tries < 32; ++tries)
                    {
                        bool taken = false;
                        for (auto k : singing)
                            taken = taken || sc.index (voices[k].degree) == sc.index (d);
                        if (! taken)
                            break;
                        d = centre[who] + between (r, -sc.n() / 2 - 1, sc.n() / 2 + 1);
                    }
                    return d;
                };
                auto sing = [&] (int d, int velocity)
                {
                    v.degree = d;
                    if (len > 0)
                        add (out.parts[who], t, len, sc.key (d), velocity);
                    v.until = t + len;
                };
                int step = v.step;
                bool listen = step == air || step == inner || step == field || step == again;
                if (step == go)
                {
                    listen = chance (r, 0.4);   // "listening then singing"
                    if (! listen)
                        step = v.tuneNext && ! singing.empty() ? tune : own;
                }
                if (step == tune && singing.empty())
                    step = own;   // nobody to tune to on this breath: a tone of one's own
                if (! listen)
                {
                    if (step == tune)
                    {
                        // The most distant voice not yet a partner (then the most distant at all).
                        size_t best = singing.front();
                        int bestScore = -1;
                        for (auto k : singing)
                        {
                            const int distance = std::abs ((int) k - (int) who) * 2
                                                 + (std::find (v.partners.begin(), v.partners.end(), (int) k) == v.partners.end() ? 16 : 0);
                            if (distance > bestScore)
                            {
                                bestScore = distance;
                                best = k;
                            }
                        }
                        v.partners.push_back ((int) best);
                        // The same pitch class, in this voice's own register.
                        const auto target = voices[best].degree;
                        auto d = target;
                        while (d - centre[who] > sc.n() / 2 + 1)
                            d -= sc.n();
                        while (centre[who] - d > sc.n() / 2 + 1)
                            d += sc.n();
                        sing (d, between (r, 46, 64));
                        ++counts[1];
                    }
                    else if (step == fresh)
                    {
                        sing (pickTone (true), between (r, 66, 84));   // heard far off
                        ++counts[2];
                    }
                    else
                    {
                        sing (pickTone (false), between (r, 46, 64));
                        ++counts[0];
                    }
                    if (v.step == go)
                        v.tuneNext = step != tune;
                }
                if (v.step < go)
                    ++v.step;
                v.next = t + breath;
            }
            mark (out, std::max (out.sections.empty() ? 0 : out.sections.back().start + 1, end - seconds (s, 2.0)), "ending when everyone else does");
            for (int k = 0; k < parts; ++k)
                if (out.parts[(size_t) k].notes.empty())
                    add (out.parts[(size_t) k], 0, std::min (seconds (s, 6.0), total), sc.key (centre[(size_t) k]), 50);
            out.end = end;
            out.parts.resize ((size_t) parts);
            out.summary = "The Tuning Meditation for " + std::to_string (parts) + (parts == 1 ? " voice: " : " voices: ") + std::to_string (counts[0])
                          + " breaths on a tone of one's own, " + std::to_string (counts[1]) + " tuned to a distant voice, " + std::to_string (counts[2])
                          + " on a new tone no one was singing";
            return out;
        }

        // Deep Listening (1989): the Deep Listening Band in the cistern at Fort Worden, with its
        // forty-five seconds of reverberation. Oliveros's sonic awareness: attention moves between
        // global (the circle: the whole field, everyone holding long tones that overlap and blend
        // in the space) and focal (the dot: one player's phrase stands out while the rest hold the
        // tonal centre underneath). In the circle each tone is one of its quarters: making a sound,
        // imagining one (a silence), listening to the sound present (joining the centre), or
        // remembering a past one; practised, it makes "complex sound masses possessing a strong
        // tonal center" (Von Gunden, The Music of Pauline Oliveros).
        // Oliveros's Expanded Instrument System repeats the accordion's and the trombone's sounds
        // through delays of different lengths, each quieter; the cistern holds every sound long after.
        Result deepListening (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            const int players = std::min (parts, 4);   // accordion, trombone, voice, didjeridu
            const std::array<int, 4> centre { 0, -sc.n(), sc.n() / 2, -2 * sc.n() };
            int64_t at = 0;
            bool global = true;
            int focalPlayer = 0;
            std::array<std::vector<int>, 4> remembered;   // what each player has sounded
            while (at < total)
            {
                auto length = std::min<int64_t> (total - at, (int64_t) between (r, global ? 6 : 4, global ? 12 : 8) * barTicks());
                if (total - at - length < 4 * barTicks())
                    length = total - at;
                if (global)
                {
                    mark (out, at, "global attention (the circle)");
                    for (int p = 0; p < players; ++p)
                    {
                        int64_t t = at + (int64_t) (r.uniform() * 2 * Q);
                        int d = centre[(size_t) p] + (p == 3 ? 0 : between (r, -1, 2));
                        auto& memory = remembered[(size_t) p];
                        while (t < at + length)
                        {
                            const auto len = std::min<int64_t> ((int64_t) between (r, 6, 18) * Q, at + length - t);
                            // The circle's quarters: making a sound, imagining one (silent), listening to
                            // the sound present (joining the tonal centre), remembering a past one.
                            const int quarter = p == 3 ? 0 : below (r, 4);
                            if (quarter == 2)
                                d = centre[(size_t) p];   // the tonic in this player's octave
                            else if (quarter == 3 && ! memory.empty())
                                d = memory[(size_t) below (r, (int) memory.size())];
                            if (quarter != 1)
                            {
                                add (out.parts[(size_t) p], t, len, sc.key (p == 3 ? centre[3] : d), between (r, 38, 58));
                                memory.push_back (d);
                            }
                            if (p != 3)
                                d = consonant (sc, centre[0], d + between (r, -1, 1), centre[(size_t) p] - 2, centre[(size_t) p] + 3, false, true, r);
                            t += std::max<int64_t> (Q, len - (int64_t) (r.uniform() * Q));   // overlapping a little: breath and bellows
                        }
                    }
                }
                else
                {
                    mark (out, at, "focal attention (the dot: " + split (info (s.method).parts)[(size_t) focalPlayer] + ")");
                    for (int p = 0; p < players; ++p)
                    {
                        auto& part = out.parts[(size_t) p];
                        if (p != focalPlayer)   // underneath, very soft: the tonal centre
                        {
                            add (part, at, length, sc.key (p == 3 ? centre[3] : centre[(size_t) p]), between (r, 24, 34));
                            continue;
                        }
                        int64_t t = at;
                        int d = centre[(size_t) p] + between (r, 0, 3);
                        int dir = 0;
                        while (t < at + length)
                        {
                            const int notes = between (r, 3, 7);
                            for (int k = 0; k < notes && t < at + length; ++k)
                            {
                                const auto len = std::min<int64_t> ((int64_t) between (r, 1, 4) * Q / 2, at + length - t);
                                add (part, t, len, sc.key (p == 3 ? centre[3] + (k % 2) * sc.fifth() : d), between (r, 64, 92));
                                d = walk (sc, d, centre[(size_t) p] - 3, centre[(size_t) p] + 5, r, dir, 0.3);
                                t += len;
                            }
                            t += (int64_t) between (r, 2, 6) * Q;   // listening between phrases
                        }
                    }
                    focalPlayer = (focalPlayer + 1 + below (r, std::max (1, players - 1))) % std::max (1, players);
                }
                at += length;
                global = ! global;
            }
            // The Expanded Instrument System: each accordion and trombone sound again after delays
            // of 1.5, 3.7 and 6.1 seconds (each its own line), each repeat softer.
            const std::array<double, 3> delays { 1.5, 3.7, 6.1 };
            for (int e = 4; e < std::min (parts, 6); ++e)
            {
                const auto source = out.parts[(size_t) (e - 4)].notes;
                for (const auto& n : source)
                    for (size_t k = 0; k < delays.size(); ++k)
                    {
                        const auto d = seconds (s, delays[k]);
                        if (n.start + d < total)
                            add (out.parts[(size_t) e], n.start + d, std::min (n.end + d, total) - n.start - d, n.key,
                                 (int) (n.velocity * std::pow (0.6, (double) k + 1)));
                    }
            }
            // The cistern: every focal phrase's notes ring on, long and soft, the space's own sound.
            if (parts >= 7)
                for (int p = 0; p < players; ++p)
                    for (const auto& n : out.parts[(size_t) p].notes)
                        if (n.velocity >= 64 && n.start < total)
                            add (out.parts[6], n.end, std::min (n.end + seconds (s, 12.0), total) - n.end, n.key, 22);
            // A conch at each turn to the whole field, low and long.
            if (parts >= 8)
                for (const auto& x : out.sections)
                    if (x.name.rfind ("global", 0) == 0)
                        add (out.parts[7], x.start, std::min<int64_t> (4 * barTicks(), total - x.start), sc.key (-sc.n() + sc.fifth()), 54);
            out.end = total;
            out.parts.resize ((size_t) parts);
            out.summary = "Deep Listening: " + std::to_string (out.sections.size()) + " turns between global and focal attention, for "
                          + std::to_string (parts) + (parts == 1 ? " part" : " parts") + (parts > 4 ? ", the Expanded Instrument System's delays" : "")
                          + (parts > 6 ? " and the cistern's ring" : "");
            return out;
        }

        //======================================================================
        // Process music: a cell written for it, then minimalize's processes on it.
        Result process (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto tri = sc.triad();
            const auto q = (int64_t) minimalize::ticksPerQuarter;
            auto cellOf = [&] (const std::vector<int>& degrees, int64_t step, int velocity, int octave)
            {
                minimalize::Cell c;
                for (size_t i = 0; i < degrees.size(); ++i)
                    if (degrees[i] > -100)
                        c.notes.push_back ({ (int64_t) i * step, (int64_t) (i + 1) * step - step / 8, sc.key (degrees[i] + octave * sc.n()), velocity, 0 });
                c.length = (int64_t) degrees.size() * step;
                return c;
            };
            // A source a part, so the processes fill exactly the parts asked for (no new voices).
            std::vector<minimalize::Part> sources;
            std::vector<minimalize::Process> processes;
            auto source = [&] (minimalize::Cell c, const char* name, minimalize::Process p)
            {
                if ((int) sources.size() < parts)
                {
                    sources.push_back ({ { std::move (c) }, name, false });
                    processes.push_back (p);
                }
            };
            // Glass's figure: the triad broken in eighths, over two beats or a bar.
            std::vector<int> figure;
            const int span = chance (r, 0.5) ? 4 : 8;
            for (int i = 0; i < span; ++i)
                figure.push_back (tri[(size_t) (i % (int) tri.size())] + (i >= (int) tri.size() && chance (r, 0.3) ? sc.n() : 0));
            source (cellOf (figure, q / 2, 84, 0), "figure", minimalize::Process::additive);
            // Reich's pattern: Piano Phase's twelve sixteenths on the mode's degrees, one copy moving
            // a sixteenth ahead at each stage against another held still.
            std::vector<int> pp { 0, 1, 4, 5, 6, 1, 0, 5, 4, 1, 6, 5 };
            for (auto& x : pp)
                x %= sc.n();
            source (cellOf (pp, q / 4, 80, 1), "pattern (phasing)", minimalize::Process::phase);
            source (cellOf (pp, q / 4, 76, 1), "pattern (held)", minimalize::Process::hold);
            // The harmony pulsed in eighths (Music for 18 Musicians).
            {
                minimalize::Cell c;
                for (int i = 0; i < 8; ++i)
                    for (auto d : tri)
                        c.notes.push_back ({ i * q / 2, i * q / 2 + q / 2 - q / 16, sc.key (d - sc.n()), 70, 0 });
                c.length = 4 * q;
                source (std::move (c), "pulse", minimalize::Process::pulse);
            }
            // The bass built up a note a stage (Drumming).
            std::vector<int> bass { 0, -100, -100, -100, 0, -100, sc.fifth(), -100 };
            source (cellOf (bass, q / 2, 90, -1), "bass", minimalize::Process::buildUp);
            // Glass's subtractive process on the figure turned round; a shorter cycle against the
            // others (they part and meet again); the harmony arpeggiated.
            std::vector<int> counter (figure.rbegin(), figure.rend());
            source (cellOf (counter, q / 2, 74, 1), "counter-figure", minimalize::Process::subtractive);
            std::vector<int> cycle (figure.begin(), figure.end() - 1);
            source (cellOf (cycle, q / 2, 72, 0), "cycle", minimalize::Process::cycles);
            source (cellOf (figure, q / 2, 70, -1), "arpeggio", minimalize::Process::arpeggio);
            minimalize::Settings ms;
            ms.style = minimalize::Style::both;
            ms.processes = processes;
            ms.repeats = std::clamp (s.bars / 16, 2, 6);
            ms.newVoices = false;
            ms.freeTracks = 0;
            ms.seed = s.seed;
            ms.maxTicks = std::max<int64_t> (s.bars, 16) * 4 * q * 2;
            const auto res = minimalize::arrange (sources, ms);
            out.parts.clear();
            for (const auto& track : res.tracks)
            {
                if ((int) out.parts.size() >= parts)
                    break;
                Part p { track.name, {}, false };
                for (const auto& n : track.notes)
                    add (p, n.start * Q / q, (n.end - n.start) * Q / q, n.pitch, n.velocity);
                out.parts.push_back (std::move (p));
            }
            for (size_t i = 0; i < res.sections.size(); ++i)
                mark (out, res.sections[i] * Q / q, "stage " + std::to_string (i + 1));
            if (out.sections.empty() || out.sections.front().start != 0)
                out.sections.insert (out.sections.begin(), { 0, "stage 1" });
            out.end = res.length * Q / q;
            out.summary = "Process music on a cell written for it: " + std::string (minimalize::name (processes.front()));
            for (size_t i = 1; i < processes.size(); ++i)
                out.summary += ", " + std::string (minimalize::name (processes[i]));
            (void) r;
            return out;
        }

        //======================================================================
        // Tintinnabuli: M-voice and T-voice, mirrored, a bell, a mensuration canon.
        Result tintinnabuli (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const auto tri = sc.triad();
            const auto note = 2 * Q;   // a half note a step
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            // The M-voice: phrases growing by a note, alternately rising from the centre and falling to it.
            std::vector<std::pair<int64_t, int>> m;   // start, degree (from the centre)
            std::vector<int64_t> phraseStarts;
            int64_t t = 0;
            for (int k = 1; t < total - 4 * note; ++k)
            {
                phraseStarts.push_back (t);
                const int len = k + 1;
                const int kind = (k - 1) % 4;   // from the centre up, down to it from above, from the centre down, up to it from below
                for (int i = 0; i < len; ++i)
                {
                    int d = 0;
                    switch (kind)
                    {
                        case 0: d = i; break;
                        case 1: d = len - 1 - i; break;
                        case 2: d = -i; break;
                        default: d = -(len - 1 - i); break;
                    }
                    m.push_back ({ t, d });
                    t += i == len - 1 ? 2 * note : note;   // the last note of a phrase held
                }
                t += note;   // a breath
            }
            const auto end = t;
            // T: the triad's nearest note above (superior), or alternately above and below.
            auto tVoice = [&] (int d, bool aboveIt)
            {
                const auto c = sc.cents (d);
                int best = aboveIt ? 99 : -99;
                for (int o = -3; o <= 3; ++o)
                    for (auto x : tri)
                    {
                        const auto td = x + o * sc.n();
                        if (aboveIt ? (sc.cents (td) > c + 1 && td < best) : (sc.cents (td) < c - 1 && td > best))
                            best = td;
                    }
                return best;
            };
            const int centre = 0;
            for (size_t i = 0; i < m.size(); ++i)
            {
                const auto st = m[i].first;
                const auto len = (i + 1 < m.size() ? std::min (m[i + 1].first, st + 2 * note) : st + 2 * note) - st;
                const auto d = centre + m[i].second;
                add (out.parts[0], st, len, sc.key (d), 72);
                add (out.parts[1], st, len, sc.key (tVoice (d, true)), 60);
                // The mirror: the M-voice upside down about the centre, an octave lower, its T-voice alternating.
                const auto md = centre - m[i].second - sc.n();
                add (out.parts[2], st, len, sc.key (md), 66);
                add (out.parts[3], st, len, sc.key (tVoice (md, i % 2 == 0)), 56);
            }
            // The bell: the tonic low at each phrase, the fifth with it now and then.
            for (size_t k = 0; k < phraseStarts.size(); ++k)
            {
                add (out.parts[4], phraseStarts[k], 3 * note, sc.key (-2 * sc.n()), 96);
                if (k % 3 == 2)
                    add (out.parts[4], phraseStarts[k], 3 * note, sc.key (sc.fifth() - 2 * sc.n()), 70);
            }
            // The mensuration canon (Cantus): the M-voice at half and a quarter of the speed, each an octave lower.
            for (int v = 0; v < 2; ++v)
            {
                const auto factor = (int64_t) (2 << v);
                for (size_t i = 0; i < m.size(); ++i)
                {
                    const auto st = m[i].first * factor;
                    if (st >= end)
                        break;
                    const auto next = i + 1 < m.size() ? m[i + 1].first * factor : st + note * factor;
                    add (out.parts[(size_t) (5 + v)], st, std::min (next, end) - st, sc.key (centre + m[i].second - (v + 1) * sc.n()), 64);
                }
            }
            add (out.parts[7], 0, end, sc.key (-2 * sc.n()), 50);   // the pedal
            mark (out, 0, "phrases growing by a note");
            for (size_t k = 4; k < phraseStarts.size(); k += 4)
                mark (out, phraseStarts[k], "phrase " + std::to_string (k + 1));
            out.end = end;
            out.parts.resize ((size_t) parts);
            out.summary = "Tintinnabuli: " + std::to_string (phraseStarts.size()) + " phrases growing by a note, the T-voice on the triad of the tonic";
            (void) r;
            return out;
        }

        //======================================================================
        // Spectral: the partials of a low fundamental, each a part in its own cents; each cycle an
        // attack and its resonance, from harmonic to stretched and back.
        Result spectral (const Settings& s, Random& r)
        {
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const std::array<int, 8> partials { 1, 3, 5, 7, 2, 9, 11, 13 };
            const double f0Key = 24 + ((s.mode.valid() ? s.mode.root : 4) + 12) % 12;   // the fundamental an octave below the bass staff
            const auto total = std::max<int64_t> (s.bars, 8) * barTicks();
            const int cycles = std::clamp (s.bars / 6, 3, 12);
            const auto cycle = total / cycles / Q * Q;
            out.microtonal = true;
            for (int k = 0; k < cycles; ++k)
            {
                // The stretch: harmonic (1) at first, most inharmonic at the golden section, harmonic again at the end.
                const auto x = (double) k / std::max (1, cycles - 1);
                const auto peak = 0.618;
                const auto stretch = 1.0 + 0.09 * (x <= peak ? x / peak : (1.0 - x) / (1.0 - peak));
                const auto at = (int64_t) k * cycle;
                mark (out, at, k == 0 ? "harmonic spectrum" : stretch > 1.06 ? "inharmonic (stretched " + std::to_string ((int) std::lround ((stretch - 1) * 1000)) + "‰)"
                                                                             : "cycle " + std::to_string (k + 1));
                for (int p = 0; p < parts; ++p)
                {
                    const auto n = partials[(size_t) p];
                    // The partial's pitch in cents above key 0: 1200 log2 (n^stretch).
                    const auto c = f0Key * 100.0 + 1200.0 * std::log2 (std::pow ((double) n, stretch));
                    const auto key = (int) std::lround (c / 100.0);
                    // The attack first (the fundamental, loud), the resonance building upward partial by
                    // partial and dying away from the top.
                    const auto enter = at + (p == 0 ? 0 : (int64_t) (cycle * 0.06 * p + r.uniform() * Q / 2));
                    const auto leave = at + (int64_t) (cycle * (0.92 - 0.05 * p));
                    const auto velocity = (int) (p == 0 ? 118 : 96 / std::sqrt ((double) n) + 30);
                    if (leave > enter)
                        add (out.parts[(size_t) p], enter, leave - enter, key, velocity, c - key * 100.0);
                }
            }
            out.end = (int64_t) cycles * cycle;
            out.parts.resize ((size_t) parts);
            out.summary = "The harmonic series of " + std::to_string ((int) std::lround (440.0 * std::pow (2.0, (f0Key - 69) / 12.0))) + " Hz in "
                          + std::to_string (cycles) + " cycles of attack and resonance, most inharmonic at the golden section";
            return out;
        }

        //======================================================================
        // Hindustani rāga: ālāp, joṛ, jhālā and a gat in tīntāl.
        Result raga (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 7);
            out.parts[2].drums = true;
            const int n = sc.n();
            const auto total = std::max<int64_t> (s.bars, 16) * barTicks();
            // Vādī and samvādī: Ga (or the degree nearest a third) and the degree a fourth or fifth from it.
            const int vadi = sc.has (386.0, 80.0) ? sc.nearest (386.0) : sc.has (702.0) ? sc.fifth() : n / 2;
            const int samvadi = sc.has (sc.cents (vadi) + 702.0 - 1200.0 * (vadi >= n / 2)) ? sc.nearest (sc.cents (vadi) + 702.0 - 1200.0 * (vadi >= n / 2)) : 0;
            const int pa = sc.has (702.0) ? sc.fifth() : sc.has (498.0) ? sc.fourth() : n - 1;   // the tānpūrā's first string
            // A pakaḍ: the rāga's catch phrase, four notes from the vādī, come back to through the piece.
            std::vector<int> pakad { vadi, sc.stepDown (vadi), vadi, sc.stepDown (sc.stepDown (vadi)) };
            int64_t t = 0;
            auto& melody = out.parts[0];
            auto& sarangi = out.parts[4];
            auto play = [&] (int64_t at, int64_t len, int d, int vel)
            {
                add (melody, at, len, sc.key (d), vel);
                if (len >= Q && chance (r, 0.7))   // the sāraṅgī shadows the line a moment later
                    add (sarangi, at + Q / 4, len - Q / 4, sc.key (d), vel - 22);
            };
            // Ālāp: badhat, the range widening a note a phrase group from mandra Sa up to tār Sa.
            mark (out, 0, "ālāp");
            const auto alapEnd = total * 30 / 100;
            int top = 1;
            int d = 0;
            while (t < alapEnd)
            {
                const int steps = between (r, 3, 6);
                for (int i = 0; i < steps && t < alapEnd; ++i)
                {
                    const bool rising = d < top && chance (r, 0.6);
                    d = rising ? sc.stepUp (d) : sc.stepDown (d);
                    d = std::clamp (d, -n + 1, top);
                    const auto len = Q * between (r, 1, 3) / (chance (r, 0.3) ? 2 : 1);
                    if (len >= Q && chance (r, 0.3))   // a kaṇ: the grace of the note above
                        add (melody, t, Q / 8, sc.key (d + 1), 50);
                    play (t + (len >= Q ? Q / 8 : 0), len, d, 70);
                    t += len + Q / 8;
                }
                // The nyās: rest on the vādī, Sa or the samvādī, held.
                const std::array<int, 3> nyas { vadi, 0, samvadi };
                d = nyas[(size_t) below (r, 3)];
                if (d > top)
                    d = 0;
                play (t, 3 * Q, d, 76);
                t += 4 * Q;
                if (chance (r, 0.5) && top < n)
                    ++top;
                if (chance (r, 0.15))   // the pakaḍ
                    for (auto x : pakad)
                    {
                        play (t, Q, x, 74);
                        t += Q;
                    }
            }
            // Joṛ: the same unfolding with a steady pulse, in eighths and quarters, to tār Sa.
            mark (out, t, "joṛ");
            const auto jorEnd = t + total * 20 / 100;
            int dir = 0;
            while (t < jorEnd)
            {
                d = walk (sc, d, -2, n + 2, r, dir, 0.12);
                const auto len = chance (r, 0.6) ? Q / 2 : Q;
                play (t, len, d, 78);
                t += len;
            }
            // Jhālā: the melody on each beat, the chikārī strings struck between in sixteenths.
            mark (out, t, "jhālā");
            const auto jhalaEnd = t + total * 12 / 100;
            for (int i = 0; t < jhalaEnd; ++i, t += Q)
            {
                d = walk (sc, d, 0, n + 3, r, dir, 0.1);
                play (t, Q / 4, d, 86);
                for (int k = 1; k < 4; ++k)
                    add (out.parts[3], t + k * Q / 4, Q / 4, sc.key (2 * n), 64 + 8 * (k == 3));
            }
            // A gat in tīntāl: sixteen beats (dha dhin dhin dha | dha dhin dhin dha | dha tin tin ta | ta dhin dhin dha),
            // the gat's sthāyī and antarā, then toṛā runs, ending in a tihāī that lands on sam.
            mark (out, t, "gat in tīntāl");
            const auto cycleLen = 16 * Q;
            const auto gatStart = t;
            const int cycles = std::max (4, (int) ((total - t) / cycleLen));
            std::vector<std::pair<int, int>> gat;   // degree, eighths
            {
                int g = 0, gdir = 0;
                int eighths = 0;
                while (eighths < 32)
                {
                    const int len = chance (r, 0.6) ? 1 : 2;
                    g = walk (sc, g, -2, n, r, gdir, 0.15);
                    gat.push_back ({ g, len });
                    eighths += len;
                }
                gat.back().first = 0;   // the mukhṛā lands on sam: Sa
            }
            const std::map<std::string, std::vector<int>> bols { { "dha", { gm::kick, gm::openHiConga } }, { "dhin", { gm::kick, gm::muteHiConga } },
                                                                 { "tin", { gm::muteHiConga } }, { "ta", { gm::openHiConga } } };
            const std::array<const char*, 16> theka { "dha", "dhin", "dhin", "dha", "dha", "dhin", "dhin", "dha", "dha", "tin", "tin", "ta", "ta", "dhin", "dhin", "dha" };
            for (int c = 0; c < cycles; ++c)
            {
                const auto at = gatStart + c * cycleLen;
                for (int b = 0; b < 16; ++b)
                    for (auto drum : bols.at (theka[(size_t) b]))
                        hit (out.parts[2], at + b * Q, drum, b == 0 ? 110 : b == 8 ? 70 : 88);
                const bool antara = c % 4 == 2;
                const bool tora = c >= 4 && c % 4 == 3 && c < cycles - 1;
                const bool last = c == cycles - 1;
                if (antara)
                    mark (out, at, "antarā");
                else if (tora)
                    mark (out, at, "toṛā");
                if (last)
                {
                    // The tihāī: a phrase three times, a beat between, its last note on the next sam.
                    mark (out, at, "tihāī");
                    const std::vector<int> phrase { sc.stepUp (sc.stepUp (0)), sc.stepUp (0), 0 };
                    const auto phraseLen = (int64_t) phrase.size() * Q / 2;
                    const auto start = at + cycleLen - (3 * phraseLen + 2 * Q) + Q / 2;
                    for (int k = 0; k < 3; ++k)
                        for (size_t i = 0; i < phrase.size(); ++i)
                            play (start + k * (phraseLen + Q) + (int64_t) i * Q / 2, Q / 2, phrase[i] + n, 96);
                    play (at + cycleLen, 4 * Q, n, 104);   // sam
                    for (auto drum : bols.at ("dha"))
                        hit (out.parts[2], at + cycleLen, drum, 120);
                    break;
                }
                int64_t x = at;
                if (tora)
                {
                    int td = n;
                    for (int i = 0; i < 64 && x < at + cycleLen; ++i, x += Q / 4)
                    {
                        td = i < 32 ? sc.stepDown (td) : sc.stepUp (td);
                        play (x, Q / 4, std::clamp (td, -n, 2 * n), 84);
                    }
                    continue;
                }
                for (const auto& [g, len] : gat)
                {
                    play (x, len * Q / 2, g + (antara ? n : 0), 84);
                    x += len * Q / 2;
                }
            }
            const auto end = gatStart + cycles * cycleLen + 4 * Q;
            // The tānpūrā throughout: Pa, Sa, Sa, low Sa, a beat each, ringing.
            const std::array<int, 4> strings { pa - n, 0, 0, -n };
            for (int64_t x = 0, i = 0; x < end; x += Q, ++i)
                add (out.parts[1], x, 4 * Q - Q / 8, sc.key (strings[(size_t) (i % 4)] - n), 52);
            // The swarmaṇḍal: the rāga's notes swept up at the start of each part.
            for (const auto& sec : out.sections)
                for (int k = 0; k <= n; ++k)
                    add (out.parts[5], sec.start + k * Q / 8, 2 * Q, sc.key (sc.stepUp (k - 1) + n), 58);
            add (out.parts[6], 0, end, sc.key (-2 * n), 46);   // the drone: low Sa
            out.end = end;
            out.parts.resize ((size_t) parts);
            out.summary = "Ālāp, joṛ and jhālā unfolding the rāga, its vādī the " + std::to_string (vadi + 1) + (vadi == 2 ? "rd" : "th")
                          + " degree, then a gat in tīntāl of " + std::to_string (cycles) + " cycles ending in a tihāī on sam";
            return out;
        }

        //======================================================================
        // Maqām: taqsīm through the ajnās, then a waṣla.
        Result maqam (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            out.parts[1].drums = true;
            out.parts[4].drums = true;
            const int n = sc.n();
            // The ghammāz: the fifth (rast, bayati's fourth when there is no fifth) where the upper jins begins.
            const int ghammaz = sc.has (702.0) ? sc.fifth() : sc.fourth();
            const auto total = std::max<int64_t> (s.bars, 16) * barTicks();
            auto& ud = out.parts[0];
            // The heterophony: each instrument its own version of the line.
            auto ensemble = [&] (int64_t at, int64_t len, int d, int vel, bool all)
            {
                add (ud, at, len, sc.key (d), vel);
                if (! all)
                    return;
                if (len >= Q / 2 && chance (r, 0.5))   // the qānūn: its tremolo on longer notes, an octave up
                {
                    for (int64_t x = at; x < at + len; x += Q / 4)
                        add (out.parts[2], x, Q / 4, sc.key (d + n), vel - 18);
                }
                else
                    add (out.parts[2], at, len, sc.key (d + n), vel - 14);
                if (len >= Q && chance (r, 0.6))   // the nāy: a grace from the note above
                    add (out.parts[3], at, Q / 8, sc.key (d + n + 1), vel - 30);
                add (out.parts[3], at + (len >= Q ? Q / 8 : 0), len - (len >= Q ? Q / 8 : 0), sc.key (d + n), vel - 16);
                add (out.parts[5], at + Q / 16, len - Q / 16, sc.key (d), vel - 20);   // the kamān, a little behind
            };
            // Taqsīm: free; the lower jins, rising to the ghammāz, the upper jins and the jawāb, a
            // modulation (the line centred a jins away), the descent and qafla.
            int64_t t = 0;
            int d = 0, dir = 0;
            const auto taqsimEnd = total * 30 / 100;
            struct Stage { const char* name; int lo, hi, rest; };
            const std::array<Stage, 5> stages { Stage { "taqsīm: the lower jins", -1, ghammaz - 1, 0 }, { "taqsīm: the ghammāz", 0, ghammaz + 1, ghammaz },
                                                { "taqsīm: the upper jins, the jawāb", ghammaz - 1, n + 1, n }, { "taqsīm: a modulation", 1, ghammaz + 2, 2 },
                                                { "taqsīm: the descent and qafla", -1, ghammaz, 0 } };
            for (size_t k = 0; k < stages.size(); ++k)
            {
                const auto& st = stages[k];
                mark (out, t, st.name);
                const auto stageEnd = t + taqsimEnd / (int64_t) stages.size();
                while (t < stageEnd)
                {
                    // A phrase, often in sequence (a motif repeated a step lower), then a rest on the stage's note.
                    std::vector<int> motif;
                    for (int i = 0, m = between (r, 3, 5); i < m; ++i)
                        motif.push_back (d = walk (sc, d, st.lo, st.hi, r, dir, 0.15));
                    const int reps = chance (r, 0.4) ? 2 : 1;
                    for (int rep = 0; rep < reps; ++rep)
                        for (auto x : motif)
                        {
                            const auto len = Q / 2 * between (r, 1, 3) / (chance (r, 0.4) ? 2 : 1);
                            ensemble (t, len, std::clamp (x - rep, st.lo - 1, st.hi), 76, false);
                            t += len;
                        }
                    d = st.rest;
                    ensemble (t, 2 * Q, d, 80, false);
                    t += 3 * Q;
                }
            }
            // The qafla: down the lower jins to the qarār, a turn below it first.
            for (auto x : { 2, 1, 0, -1, 0 })
            {
                ensemble (t, x == 0 ? Q : Q / 2, x, 84, false);
                t += x == 0 ? Q : Q / 2;
            }
            t += 2 * Q;
            add (out.parts[7], 0, t, sc.key (-2 * n), 44);   // the qarār held under the taqsīm
            // Samāʿī thaqīl, 10/8: dum . . tak . dum dum tak . . ; four khānāt, each followed by the taslīm.
            const std::map<char, int> darbukka { { 'd', gm::lowConga }, { 't', gm::openHiConga }, { 'k', gm::muteHiConga } };
            const std::map<char, int> riqq { { 's', gm::tambourine }, { 't', gm::highTimbale } };
            const auto eighth = Q / 2;
            auto phraseIn = [&] (int eighths, int lo, int hi, std::vector<std::pair<int, int>>& outPhrase)
            {
                int e = 0;
                while (e < eighths)
                {
                    const int len = std::min (eighths - e, chance (r, 0.55) ? 1 : 2);
                    d = walk (sc, d, lo, hi, r, dir, 0.12);
                    outPhrase.push_back ({ d, len });
                    e += len;
                }
            };
            std::vector<std::pair<int, int>> taslim;
            phraseIn (10 * std::clamp (s.bars / 16, 1, 4), -1, ghammaz + 1, taslim);   // a cycle of 10/8 to every 16 bars asked for, up to four
            taslim.back().first = 0;
            auto playPhrase = [&] (int64_t at, const std::vector<std::pair<int, int>>& phrase, int64_t unit, int vel)
            {
                int64_t x = at;
                for (const auto& [pd, len] : phrase)
                {
                    ensemble (x, len * unit, pd, vel, true);
                    x += len * unit;
                }
                return x;
            };
            auto drums = [&] (int64_t at, int cycles, int64_t unit, const std::string& dum, const std::string& jingle)
            {
                for (int c = 0; c < cycles; ++c)
                {
                    pattern (out.parts[1], at + c * (int64_t) dum.size() * unit, unit, dum, darbukka, 92);
                    pattern (out.parts[4], at + c * (int64_t) jingle.size() * unit, unit, jingle, riqq, 70);
                }
            };
            const int khanaCycles = std::clamp (s.bars / 16, 1, 4);
            const std::array<std::pair<int, int>, 4> khanaRange { std::pair { -1, ghammaz }, { 0, ghammaz + 2 }, { ghammaz - 1, n + 2 }, { -1, ghammaz + 1 } };
            for (int k = 0; k < 4; ++k)
            {
                mark (out, t, "samāʿī: khāna " + std::to_string (k + 1));
                std::vector<std::pair<int, int>> khana;
                if (k < 3)
                {
                    phraseIn (10 * khanaCycles, khanaRange[(size_t) k].first, khanaRange[(size_t) k].second, khana);
                    drums (t, khanaCycles, eighth, "D..T.DDT..", "sstssstsss");
                    t = playPhrase (t, khana, eighth, 84);
                }
                else   // the fourth in samāʿī dārij, 3/4, quicker
                {
                    phraseIn (6 * khanaCycles + 6, khanaRange[3].first, khanaRange[3].second, khana);
                    drums (t, khanaCycles + 1, eighth, "D.TT.T", "stssts");
                    t = playPhrase (t, khana, eighth, 88);
                }
                mark (out, t, "samāʿī: taslīm");
                drums (t, khanaCycles, eighth, "D..T.DDT..", "sstssstsss");
                t = playPhrase (t, taslim, eighth, 86);
            }
            // A muwashshaḥ in maqsūm (dum tak . tak dum . tak .): the dawr twice, the khāna, the qafla.
            std::vector<std::pair<int, int>> dawr, khana;
            phraseIn (8 * khanaCycles, -1, ghammaz + 1, dawr);
            dawr.back().first = 0;
            phraseIn (8 * khanaCycles, ghammaz - 1, n + 2, khana);
            auto sing = [&] (int64_t at, const std::vector<std::pair<int, int>>& phrase)
            {
                int64_t x = at;
                for (const auto& [pd, len] : phrase)
                {
                    add (out.parts[6], x, len * eighth, sc.key (pd), 90);
                    x += len * eighth;
                }
            };
            for (const auto& [name, phrase] : { std::pair { "muwashshaḥ: dawr", &dawr }, { "muwashshaḥ: dawr again", &dawr },
                                                { "muwashshaḥ: khāna", &khana }, { "muwashshaḥ: qafla", &dawr } })
            {
                mark (out, t, name);
                drums (t, khanaCycles, eighth, "DT.TD.T.", "sts.sts.");
                sing (t, *phrase);
                t = playPhrase (t, *phrase, eighth, 80);
            }
            // The closing qafla: all together down to the qarār.
            mark (out, t, "qafla");
            for (auto x : { ghammaz, ghammaz - 1, 2, 1, 0 })
            {
                ensemble (t, x == 0 ? 3 * Q : Q / 2, x, 96, true);
                add (out.parts[6], t, x == 0 ? 3 * Q : Q / 2, sc.key (x), 96);
                t += x == 0 ? 3 * Q : Q / 2;
            }
            hit (out.parts[1], t - 3 * Q, gm::lowConga, 118);
            out.end = t;
            out.parts.resize ((size_t) parts);
            out.summary = "A taqsīm in five stages through the ajnās (the ghammāz on the " + std::to_string (ghammaz + 1) + "th degree), then a waṣla: "
                          "a samāʿī of four khānāt with its taslīm, a muwashshaḥ in maqsūm and the qafla";
            return out;
        }

        //======================================================================
        // Persian radif: the dastgāh's gushe-hā from darāmad to owj and forud.
        Result radif (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 6);
            out.parts[2].drums = true;
            const int n = sc.n();
            const auto total = std::max<int64_t> (s.bars, 16) * barTicks();
            const std::map<char, int> tonbak { { 't', gm::lowConga }, { 'b', gm::openHiConga }, { 'k', gm::muteHiConga }, { 'r', gm::rim } };
            auto& tar = out.parts[0];
            auto& voice = out.parts[1];
            int64_t t = 0;
            int d = 0, dir = 0;
            // Pishdarāmad: the ensemble's metred overture in 6/8 on the darāmad's notes.
            mark (out, 0, "pishdarāmad");
            const auto eighth = Q / 2;
            for (int bar = 0; bar < std::max (4, s.bars / 8); ++bar)
            {
                pattern (out.parts[2], t, eighth, bar % 4 == 3 ? "TbkTbk" : "T.bT.b", tonbak, 86);
                for (int i = 0; i < 6; i += chance (r, 0.5) ? 1 : 2)
                {
                    d = walk (sc, d, -1, n / 2 + 1, r, dir, 0.1);
                    add (tar, t + i * eighth, eighth, sc.key (d), 80);
                    add (out.parts[3], t + i * eighth, eighth, sc.key (d + n), 70);
                    add (out.parts[4], t + i * eighth, eighth, sc.key (d), 64);
                }
                t += 6 * eighth;
            }
            // Āvāz: the gushe-hā, each on its shāhed, rising; the voice's phrase, then the tār's javāb.
            struct Gushe { std::string name; int shahed, ist; bool metred; };
            std::vector<Gushe> gushes { { "darāmad", 1, 0, false }, { "kereshmeh", 2, 1, true } };
            const int rising = std::clamp (s.bars / 12, 1, 4);
            for (int k = 0; k < rising; ++k)
            {
                const int sh = std::min (n, 3 + k * std::max (1, (n - 2) / rising));
                gushes.push_back ({ "gushe on the " + std::to_string (sh + 1) + "th degree", sh, sh - 1, false });
            }
            gushes.push_back ({ "owj", n + 1, n, false });
            gushes.push_back ({ "forud", 1, 0, false });
            const auto avazEnd = total * 75 / 100;
            const auto each = (avazEnd - t) / (int64_t) gushes.size();
            for (const auto& g : gushes)
            {
                mark (out, t, g.name);
                const auto gEnd = t + each;
                if (g.metred)   // kereshmeh: the hemiola rhythm of 6/8, short long short long long
                {
                    for (; t < gEnd; t += 6 * eighth)
                    {
                        const std::array<int, 5> lens { 1, 2, 1, 1, 1 };
                        int64_t x = t;
                        for (auto len : lens)
                        {
                            d = walk (sc, d, g.ist - 2, g.shahed + 2, r, dir, 0.1);
                            add (voice, x, len * eighth, sc.key (d), 84);
                            add (tar, x, len * eighth, sc.key (d), 72);
                            x += len * eighth;
                        }
                        pattern (out.parts[2], t, eighth, "TbkTb.", tonbak, 80);
                    }
                    continue;
                }
                while (t < gEnd)
                {
                    // The voice: a phrase circling the shāhed, coming to rest on the ist (forud: on the darāmad's final).
                    std::vector<std::pair<int, int64_t>> phrase;
                    d = g.shahed;
                    for (int i = 0, m = between (r, 4, 7); i < m; ++i)
                    {
                        d = walk (sc, d, g.ist - 2, g.shahed + 2, r, dir, 0.1);
                        phrase.push_back ({ d, Q / 2 * between (r, 1, 4) });
                    }
                    phrase.push_back ({ g.ist, 3 * Q });
                    if (g.name == "forud")   // the forud: down by step to the final
                        for (int x = g.shahed + 2; x >= 0; --x)
                            phrase.push_back ({ x, x == 0 ? 3 * Q : Q / 2 });
                    for (const auto& [pd, len] : phrase)
                    {
                        add (voice, t, len, sc.key (pd), 86);
                        if (len >= Q)   // the santur rings the long notes
                            add (out.parts[3], t, len, sc.key (pd + n), 56);
                        t += len;
                    }
                    t += Q / 2;
                    // The javāb: the tār answers, the phrase again a little varied.
                    for (const auto& [pd, len] : phrase)
                    {
                        const auto vd = chance (r, 0.25) ? sc.stepUp (pd) : pd;
                        add (tar, t, len * 3 / 4, sc.key (vd), 76);
                        add (out.parts[4], t + Q / 8, len * 3 / 4, sc.key (vd), 58);
                        t += len * 3 / 4;
                    }
                    t += Q;
                }
            }
            // Chahārmezrāb: quick 6/8, the melody against a pedal on the final, in sixteenths.
            mark (out, t, "chahārmezrāb");
            const auto sixteenth = Q / 4;
            const auto chaharEnd = t + (total - t) / 2;
            for (int i = 0; t < chaharEnd; ++i, t += sixteenth)
            {
                if (i % 2 == 0)
                    d = walk (sc, d, 0, n + 2, r, dir, 0.08);
                const auto key = i % 2 == 0 ? sc.key (d) : sc.key (0);
                add (tar, t, sixteenth, key, i % 2 == 0 ? 84 : 62);
                add (out.parts[3], t, sixteenth, key + sc.m.period, i % 2 == 0 ? 76 : 56);
                if (i % 12 == 0)
                    pattern (out.parts[2], t, eighth, "T.bTkb", tonbak, 84);
            }
            // Reng: the dance in 6/8 to close, the ney with the tār.
            mark (out, t, "reng");
            for (; t < total; t += 6 * eighth)
            {
                pattern (out.parts[2], t, eighth, "T.bT.b", tonbak, 90);
                for (int i = 0; i < 6; i += 2)
                {
                    d = walk (sc, d, -1, n + 1, r, dir, 0.1);
                    add (tar, t + i * eighth, i == 4 ? eighth : 2 * eighth, sc.key (d), 84);
                    add (out.parts[5], t + i * eighth, 2 * eighth, sc.key (d + n), 72);
                    add (out.parts[4], t + i * eighth, 2 * eighth, sc.key (d), 66);
                }
            }
            add (tar, t, 3 * Q, sc.key (0), 92);
            add (out.parts[5], t, 3 * Q, sc.key (n), 80);
            hit (out.parts[2], t, gm::lowConga, 110);
            out.end = t + 3 * Q;
            out.parts.resize ((size_t) parts);
            out.summary = "The radif's order: a pishdarāmad, " + std::to_string (gushes.size()) + " gushe-hā from the darāmad to the owj and the forud, "
                          "then a chahārmezrāb and a reng";
            return out;
        }

        //======================================================================
        // Javanese gamelan: a ladrang, its balungan elaborated, the gong cycle marking it.
        Result gamelan (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            out.parts[6].drums = true;
            const int n = sc.n();
            // The balungan: four kenongan of two gatra (32 beats, a ladrang's gongan); each gatra's last
            // note its seleh, the gongan's last the gong tone.
            const int gongTone = 0;
            auto gongan = [&] (int last)
            {
                std::vector<int> b;
                int d = between (r, 0, n - 1), dir = 0;
                for (int g = 0; g < 8; ++g)
                {
                    for (int i = 0; i < 3; ++i)
                        b.push_back (d = walk (sc, d, -1, n, r, dir, 0.25));
                    // The seleh: the gong tone at the end, the kenong's tones a fifth or the third away at the kenongan.
                    const int seleh = g == 7 ? last : g % 2 == 1 ? (chance (r, 0.5) ? sc.fifth() : sc.triad().size() > 1 ? sc.triad()[1] : 1) : between (r, 0, n - 1);
                    b.push_back (d = seleh);
                }
                return b;
            };
            const auto a = gongan (gongTone), bSection = gongan (gongTone);
            auto& balungan = out.parts[0];
            int64_t t = 0;
            // Buka: the bonang alone plays the last gatra, the gong falls on the first beat after it... as the
            // gongan's last beat.
            mark (out, 0, "buka");
            for (int i = 0; i < 4; ++i)
                add (out.parts[2], t + i * Q, Q, sc.key (a[(size_t) (28 + i)] + n), 84);
            t += 4 * Q;
            hit (out.parts[6], t - Q / 2, gm::lowConga, 90);
            add (out.parts[4], t - Q / 4, 8 * Q, sc.key (gongTone - 2 * n), 110);   // the gong on the buka's seleh
            // The gongan: irama I (lancar), irama II (each beat twice as long, the elaboration twice as dense), and back.
            struct Pass { const std::vector<int>* b; int irama; const char* name; };
            // A gongan is 8 bars in irama I, 16 in irama II, about 12 in the suwuk.
            std::vector<Pass> passes { { &a, 1, "ladrang, irama I" }, { &a, 2, "irama II (dados)" } };
            if (s.bars >= 60)
                passes.push_back ({ &bSection, 2, "irama II, the ngelik" });
            for (int left = s.bars - 36 - (s.bars >= 60 ? 16 : 0); left >= 8; left -= 8)
                passes.insert (passes.begin() + 1, { &a, 1, "ladrang, irama I" });
            if (s.bars >= 60)
                passes.push_back ({ &a, 1, "irama I again" });
            passes.push_back ({ &a, 1, "suwuk (slowing to the gong)" });
            for (size_t pi = 0; pi < passes.size(); ++pi)
            {
                const auto& pass = passes[pi];
                const auto& b = *pass.b;
                const bool suwuk = pi + 1 == passes.size();
                mark (out, t, pass.name);
                for (int beat = 0; beat < 32; ++beat)
                {
                    // The beat's length: irama II twice as long; the suwuk slowing to half speed by the gong.
                    auto beatLen = (int64_t) (Q * pass.irama * (suwuk ? 1.0 + beat / 31.0 : 1.0));
                    const int d = b[(size_t) beat];
                    const int next = b[(size_t) ((beat + 1) % 32)];
                    add (balungan, t, beatLen, sc.key (d), 82);
                    // Saron peking: each balungan note twice, ahead of it (nacah: the pair a beat of the next).
                    const int perBeat = 2 * pass.irama;
                    for (int k = 0; k < perBeat; ++k)
                        add (out.parts[1], t + k * beatLen / perBeat, beatLen / perBeat, sc.key ((k < perBeat / 2 ? d : next) + n), 70);
                    // Bonang barung, mipil: the gatra's notes in pairs, a b a b, c d c d, at twice (irama II four times) the balungan's density.
                    const int gatra = beat / 4 * 4;
                    const int pairA = b[(size_t) (gatra + (beat % 4 < 2 ? 0 : 2))], pairB = b[(size_t) (gatra + (beat % 4 < 2 ? 1 : 3))];
                    for (int k = 0; k < perBeat; ++k)
                        add (out.parts[2], t + k * beatLen / perBeat, beatLen / perBeat, sc.key ((k % 2 == 0 ? pairA : pairB) + n), 74);
                    // Gendèr: the line in octaves around it, sustained.
                    if (beat % 2 == 1)
                        add (out.parts[7], t, 2 * beatLen, sc.key (d - n), 60);
                    // The colotomy of a ladrang: kenong on beats 8, 16, 24, 32; kempul on 12, 20, 28; gong on 32;
                    // kethuk on 2, 6, 10..., kempyang on the beats between.
                    const int b1 = beat + 1;
                    if (b1 % 8 == 0)
                        add (out.parts[3], t, beatLen * 4, sc.key (d), 96);
                    if (b1 == 32)
                        add (out.parts[4], t, beatLen * 8, sc.key (d - 2 * n), 118);
                    else if (b1 % 8 == 4 && b1 > 4)
                        add (out.parts[4], t, beatLen * 2, sc.key (d - n), 88);
                    if (b1 % 4 == 2)
                        add (out.parts[5], t, beatLen / 2, sc.key (1 - n), 76);
                    else if (b1 % 2 == 1)
                        add (out.parts[5], t + beatLen / 2, beatLen / 2, sc.key (n), 54);
                    // Kendhang: dhah and tak, the angsel leading to the gong.
                    hit (out.parts[6], t, b1 % 4 == 0 ? gm::lowConga : gm::rim, b1 % 4 == 0 ? 92 : 64, beatLen / 2);
                    if (pass.irama == 2 || b1 >= 29)
                        hit (out.parts[6], t + beatLen / 2, gm::muteHiConga, 70, beatLen / 2);
                    t += beatLen;
                }
            }
            out.end = t + 8 * Q;
            out.parts.resize ((size_t) parts);
            out.summary = "A ladrang in " + std::to_string (passes.size()) + " gongan: a buka, irama I and II (the ngelik), and a suwuk, "
                          "the balungan elaborated by peking and bonang in mipil, the kenong, kempul, kethuk and gong marking the cycle";
            return out;
        }

        //======================================================================
        // Jo-ha-kyū in the gagaku ensemble.
        Result joHaKyu (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            for (int p : { 3, 6, 7 })
                out.parts[(size_t) p].drums = true;
            const int n = sc.n();
            const auto total = std::max<int64_t> (s.bars, 12) * barTicks();
            int d = 0, dir = 0;
            int64_t t = 0;
            // One melody note with its layers: hichiriki heterophony (approached from below), the shō's
            // aitake (the note in a cluster of the mode's notes above it), koto and biwa on the downbeats.
            auto layer = [&] (int64_t at, int64_t len, int deg, bool downbeat, int vel)
            {
                add (out.parts[0], at, len, sc.key (deg + n), vel);
                if (len >= Q / 2 && chance (r, 0.6))   // the hichiriki's enbai: a slide from below
                    add (out.parts[1], at, len / 6, sc.key (deg - 1), vel - 20);
                add (out.parts[1], at + (len >= Q / 2 ? len / 6 : 0), len - (len >= Q / 2 ? len / 6 : 0), sc.key (deg), vel - 6);
                for (auto x : { 0, 1, 3, 4, 5 })
                    add (out.parts[2], at, len, sc.key (deg + x + n), 46);
                if (downbeat)
                {
                    add (out.parts[4], at, len, sc.key (deg - n), 66);       // koto: the note and its octave
                    add (out.parts[4], at + len / 3, len * 2 / 3, sc.key (deg), 60);
                    for (int k = 0; k < 4; ++k)                             // biwa: a chord swept upward
                        add (out.parts[5], at + k * Q / 16, len / 2, sc.key (deg - n + (k == 0 ? -n : 0) + k * 2), 58 + 4 * k);
                }
            };
            // Jo: unmetred, the netori: the shō alone, then the hichiriki and flute with free phrases, slow.
            mark (out, 0, "jo (netori, unmetred)");
            const auto joEnd = total / 5;
            while (t < joEnd)
            {
                const auto len = Q * between (r, 2, 5);
                d = walk (sc, d, -1, n + 1, r, dir, 0.15);
                layer (t, len, d, chance (r, 0.3), 64);
                if (chance (r, 0.3))
                    hit (out.parts[3], t, gm::highTimbale, 50);   // a kakko's single stroke (sei)
                t += len + Q / 2;
            }
            // Ha: metred in cycles of four beats, the beat quickening from slow to moderate; the kakko's
            // rolls quicken into the beat (katarai), the taiko at each cycle's end, the shōko on 1 and 3.
            auto metred = [&] (int64_t from, int64_t to, double beatFrom, double beatTo, const char* name)
            {
                mark (out, from, name);
                int64_t x = from;
                for (int cyc = 0; x < to; ++cyc)
                {
                    const auto progress = (double) (x - from) / std::max<int64_t> (1, to - from);
                    const auto beat = (int64_t) (Q * (beatFrom + (beatTo - beatFrom) * progress));
                    for (int b = 0; b < 4; ++b)
                    {
                        d = walk (sc, d, -1, n + 2, r, dir, 0.15);
                        layer (x + b * beat, beat, d, b == 0, 74 + b * 2);
                        if (b == 0 || b == 2)
                            hit (out.parts[7], x + b * beat, gm::rideBell, 70);
                    }
                    for (int k = 0; k < 4; ++k)   // katarai: strokes quickening into the cycle
                        hit (out.parts[3], x + 3 * beat + beat * (1.0 - 1.0 / (k + 1)), gm::highTimbale, 56 + k * 6, beat / 8);
                    hit (out.parts[6], x + 3 * beat, gm::acousticKick, 70);   // taiko: the soft stroke, then the loud on the next downbeat
                    hit (out.parts[6], x + 4 * beat, gm::lowFloorTom, 108);
                    x += 4 * beat;
                }
                return x;
            };
            t = metred (t, t + total * 45 / 100, 2.0, 1.1, "ha (metred, quickening)");
            t = metred (t, total - 4 * Q, 0.9, 0.55, "kyū (fast)");
            // The close: a sudden broadening, the flute alone holding the final (tomede).
            mark (out, t, "tomede (the close)");
            layer (t, 2 * Q, 1, true, 70);
            add (out.parts[0], t + 2 * Q, 4 * Q, sc.key (n), 60);
            hit (out.parts[6], t, gm::lowFloorTom, 112);
            out.end = t + 6 * Q;
            out.parts.resize ((size_t) parts);
            out.summary = "Jo-ha-kyū: an unmetred jo (netori), a ha quickening from half speed and a fast kyū, closed by the flute alone";
            return out;
        }

        //======================================================================
        // Sanjo: the jangdan from slow to fast.
        Result sanjo (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            out.parts[1].drums = true;
            out.parts[7].drums = true;
            const int n = sc.n();
            const auto total = std::max<int64_t> (s.bars, 16) * barTicks();
            // Each jangdan: its beats, the subdivision of a beat, a beat's length, its janggu pattern (a
            // character a subdivision: d deong both sides, k kung the drum head, t deok the stick, r a roll),
            // and its share of the piece.
            struct Jangdan { const char* name; int beats, sub; double beat; const char* janggu; double share; };
            const std::array<Jangdan, 5> all {
                Jangdan { "jinyangjo", 6, 3, 2.4, "d.....k.....t..k..t..r.......k..t..", 0.30 },
                { "jungmori", 12, 2, 1.0, "d...k.t.k...t.k.t...k...", 0.22 },
                { "jungjungmori", 4, 3, 1.05, "d..k.tt.kt..", 0.18 },
                { "jajinmori", 4, 3, 0.6, "d.kt.kd.kt.k", 0.17 },
                { "hwimori", 4, 2, 0.45, "d.ktd.kt", 0.13 },
            };
            const std::map<char, int> kit { { 'd', gm::kick }, { 'k', gm::lowTom }, { 't', gm::rim }, { 'r', gm::rim } };
            int64_t t = 0;
            int d = 0, dir = 0;
            for (const auto& j : all)
            {
                mark (out, t, j.name);
                const auto beat = (int64_t) (Q * j.beat);
                const auto subLen = beat / j.sub;
                const auto cycle = beat * j.beats;
                const auto until = t + std::max (cycle, (int64_t) (total * j.share) / cycle * cycle);
                for (int cyc = 0; t < until; ++cyc, t += cycle)
                {
                    // The janggu: its pattern, the d's doubled by the buk.
                    const std::string pat (j.janggu);
                    for (int i = 0; i < j.beats * j.sub; ++i)
                    {
                        const auto c = pat[(size_t) (i % (int) pat.size())];
                        if (kit.count (c) != 0)
                        {
                            hit (out.parts[1], t + i * subLen, kit.at (c), c == 'd' ? 100 : 76, subLen);
                            if (c == 'r')
                                hit (out.parts[1], t + i * subLen + subLen / 2, gm::rim, 60, subLen / 2);
                            if (c == 'd')
                                hit (out.parts[7], t + i * subLen, gm::acousticKick, 90, subLen);
                        }
                    }
                    // The melody: maeggo (tie: tension, higher, off the beat) for most of the cycle, pulgo
                    // (release: down to the tonic on the last beat).
                    const bool tension = cyc % 2 == 0;
                    int64_t x = t;
                    while (x < t + cycle - beat)
                    {
                        const auto len = subLen * between (r, 1, j.beat > 2 ? 4 : 3);
                        d = walk (sc, d, tension ? 1 : -1, tension ? n + 2 : n, r, dir, 0.15);
                        const auto at = x + (tension && chance (r, 0.3) ? subLen / 2 : 0);   // syncopated
                        const auto l = std::min (len, t + cycle - beat - at);
                        if (l > 0)
                        {
                            add (out.parts[0], at, l, sc.key (d), tension ? 86 : 76);
                            // Sanjo hapju: each instrument its own version, fewer notes or held, in its octave.
                            if (chance (r, 0.5))
                                add (out.parts[2], at, l, sc.key (d - n), 72);       // geomungo, on the main notes
                            add (out.parts[3], at, l, sc.key (d + n), 70);           // daegeum
                            add (out.parts[4], at + subLen / 4, l - subLen / 4, sc.key (d), 66);   // haegeum, a little behind
                            if (chance (r, 0.35))
                                add (out.parts[5], at, 2 * l, sc.key (d - n), 60);   // ajaeng, held
                            add (out.parts[6], at, l, sc.key (d), 74);               // piri
                        }
                        x += len;
                    }
                    const int home = tension ? sc.fifth() : 0;
                    d = home;
                    for (int p : { 0, 3, 4, 6 })
                        add (out.parts[(size_t) p], t + cycle - beat, beat, sc.key (home + (p == 3 ? n : 0)), 80);
                    add (out.parts[2], t + cycle - beat, beat, sc.key (home - n), 74);
                }
            }
            out.end = t;
            out.parts.resize ((size_t) parts);
            out.summary = std::string (parts > 2 ? "A sanjo hapju" : "A sanjo") + " through five jangdan, jinyangjo to hwimori, each cycle tied in tension and released";
            return out;
        }

        //======================================================================
        // A Chinese suite: one qupai in four phrases (qǐ chéng zhuǎn hé), played free, slow, moderate, fast, free.
        Result chineseSuite (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            out.parts[3].drums = true;
            out.parts[7].drums = true;
            const int n = sc.n();
            // The tune: four phrases of eight notes, each starting on the note the last ended on (yú yǎo wěi);
            // chéng begins as qǐ does, zhuǎn turns higher, hé ends as qǐ did, on the final.
            std::vector<std::vector<int>> phrases (4);
            int d = 0, dir = 0;
            for (int p = 0; p < 4; ++p)
            {
                auto& ph = phrases[(size_t) p];
                ph.push_back (p == 0 ? 0 : phrases[(size_t) p - 1].back());
                const int lo = p == 2 ? 1 : -2, hi = p == 2 ? n + 3 : n + 1;
                d = ph.back();
                for (int i = 1; i < 8; ++i)
                {
                    if (p == 1 && i < 4)
                        d = phrases[0][(size_t) i];          // chéng: as qǐ began
                    else if (p == 3 && i >= 4)
                        d = phrases[0][(size_t) i];          // hé: as qǐ ended
                    else
                        d = walk (sc, d, lo, hi, r, dir, 0.2);
                    ph.push_back (d);
                }
            }
            phrases[3].back() = 0;
            const char* const names[] { "qǐ", "chéng", "zhuǎn", "hé" };
            const std::map<char, int> ban { { 'b', gm::highWood }, { 'g', gm::lowTom }, { 'x', gm::snare } };
            int64_t t = 0;
            // One note in the ensemble: the erhu, the pipa (its lunzhi tremolo on long notes), the dizi an octave up with a grace,
            // the yangqin in octaves, the ruan on the strong notes.
            auto ensemble = [&] (int64_t at, int64_t len, int deg, bool strong, int vel)
            {
                add (out.parts[0], at, len, sc.key (deg), vel);
                if (len >= Q)
                    for (int64_t x = at; x < at + len; x += Q / 4)
                        add (out.parts[1], x, Q / 4, sc.key (deg), vel - 24);
                else
                    add (out.parts[1], at, len, sc.key (deg), vel - 12);
                if (len >= Q / 2)
                    add (out.parts[4], at, Q / 16, sc.key (deg + n + 1), vel - 30);
                add (out.parts[4], at + (len >= Q / 2 ? Q / 16 : 0), len - (len >= Q / 2 ? Q / 16 : 0), sc.key (deg + n), vel - 14);
                add (out.parts[5], at, len / 2, sc.key (deg + n), vel - 20);
                add (out.parts[5], at, len / 2, sc.key (deg), vel - 26);
                if (strong)
                    add (out.parts[6], at, len, sc.key (deg - 2 * n + (deg < 0 ? n : 0)), vel - 10);
            };
            // The guzheng's sweep: the mode's notes up an octave and a half, at each phrase's end.
            auto sweep = [&] (int64_t at, int from)
            {
                for (int k = 0; k < n + n / 2; ++k)
                    add (out.parts[2], at + k * Q / 12, Q, sc.key (from + k), 62 + k);
            };
            // Free: each note held as long as it is felt, the drum rolling.
            auto free = [&] (const char* name, bool rit)
            {
                mark (out, t, name);
                for (const auto& ph : { phrases[0], phrases[3] })
                {
                    for (size_t i = 0; i < ph.size(); ++i)
                    {
                        const auto len = (int64_t) (Q * (0.5 + 2.0 * r.uniform()) * (rit ? 1.0 + i / 6.0 : 1.0));
                        ensemble (t, len, ph[i], i == 0, 70);
                        t += len;
                    }
                    sweep (t - Q, ph.back() - n);
                    t += Q;
                }
                hit (out.parts[7], t - Q, gm::chineseCymbal, 90, 2 * Q);
            };
            free ("sǎn (free)", false);
            // Banshi: the tune in màn (each note two beats, flowers added: passing notes), zhōng (a beat) and kuài (an eighth, twice through).
            struct Ban { const char* name; int64_t unit; bool flowers; int repeats; const char* clapper; };
            const std::array<Ban, 3> bans { Ban { "màn (slow)", 2 * Q, true, 1, "b...g...x...g..." }, { "zhōng (moderate)", Q, false, std::max (1, s.bars / 24), "b.g.x.g." },
                                            { "kuài (fast)", Q / 2, false, std::max (1, s.bars / 12), "bgxg" } };
            for (const auto& b : bans)
                for (int rep = 0; rep < b.repeats; ++rep)
                    for (int p = 0; p < 4; ++p)
                    {
                        mark (out, t, std::string (b.name) + ": " + names[p]);
                        const auto& ph = phrases[(size_t) p];
                        const auto phraseLen = (int64_t) ph.size() * b.unit;
                        for (int64_t x = 0; x < phraseLen; x += (int64_t) std::strlen (b.clapper) * Q / 4)
                            pattern (out.parts[3], t + x, Q / 4, b.clapper, ban, 80);
                        for (size_t i = 0; i < ph.size(); ++i)
                        {
                            const auto at = t + (int64_t) i * b.unit;
                            if (b.flowers && i + 1 < ph.size() && std::abs (ph[i + 1] - ph[i]) >= 2)   // jiāhuā: a passing note between
                            {
                                ensemble (at, b.unit / 2, ph[i], i % 2 == 0, 78);
                                ensemble (at + b.unit / 2, b.unit / 2, ph[i] + (ph[i + 1] > ph[i] ? 1 : -1), false, 70);
                            }
                            else if (b.unit <= Q / 2 && chance (r, 0.3))   // kuài: a note struck twice
                            {
                                ensemble (at, b.unit / 2, ph[i], i % 2 == 0, 86);
                                ensemble (at + b.unit / 2, b.unit / 2, ph[i], false, 76);
                            }
                            else
                                ensemble (at, b.unit, ph[i], i % 2 == 0, 82);
                        }
                        if (p == 3)
                            hit (out.parts[7], t + phraseLen - b.unit, gm::chineseCymbal, 100, 2 * Q);
                        if (b.unit >= Q)
                            sweep (t + phraseLen - b.unit, ph.back() - n);
                        t += phraseLen;
                    }
            free ("sǎn (free, slowing)", true);
            out.end = t + Q;
            out.parts.resize ((size_t) parts);
            out.summary = "One qupai of four phrases (qǐ chéng zhuǎn hé, each biting the last one's tail) through sǎn, màn, zhōng, kuài and sǎn";
            return out;
        }

        //======================================================================
        // Ewe drumming: layered ostinati on the bell's timeline.
        Result ostinato (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            for (int p = 0; p < 6; ++p)
                out.parts[(size_t) p].drums = true;
            const auto pulse = Q / 2;                  // twelve pulses a cycle: a bar of 12/8
            const auto cycle = 12 * pulse;
            const int cycles = std::max (12, (int) (std::max<int64_t> (s.bars, 8) * barTicks() / cycle));
            // The parts, each a pattern of the twelve pulses.
            const std::string bell = "H.L.LL.L.L.L";        // the standard pattern (gankogui): seven strokes, the low bell on one
            const std::string axatse = "XxXxXXxXxXxX";      // down on the bell's strokes, up between
            const std::string kaganu = "..k..k..k..k";      // off the beat, always
            const std::array<std::string, 2> kidi { "..oo..m.oo.m", "o..o..o..o.." }, sogo { "..b..m..b..m", "b.m.b.m.b.m." };
            const std::map<char, int> kit { { 'h', gm::highAgogo }, { 'l', gm::lowAgogo }, { 'x', gm::maracas }, { 'k', gm::highTimbale },
                                            { 'o', gm::openHiConga }, { 'm', gm::muteHiConga }, { 'b', gm::lowMidTom }, { 'n', gm::lowTom },
                                            { 'a', gm::highTom }, { 'c', gm::hiMidTom }, { 'r', gm::rim } };
            // The lead drum's vocabulary: figures of three against the bell's four, two, and the calls.
            const std::array<std::string, 4> figures { "a..a..a..a..", "c.a.c.a.c.a.", "a.ca..a.ca..", "..a..a.cc.a." };
            const std::string call = "AaAa.cA.a.AA";
            // The song: a cantor's call over two cycles, the chorus's answer a third below.
            std::vector<std::pair<int, int>> song;   // degree, pulses
            {
                int d = sc.fifth(), dir = 0, p = 0;
                while (p < 24)
                {
                    const int len = std::min (24 - p, chance (r, 0.5) ? 2 : 3);
                    d = walk (sc, d, -1, sc.n() + 1, r, dir, 0.2);
                    song.push_back ({ d, len });
                    p += len;
                }
                song.back().first = 0;
            }
            int response = 0, figure = 0;
            for (int c = 0; c < cycles; ++c)
            {
                const auto at = (int64_t) c * cycle;
                // The layers enter one by one: bell, rattle, kaganu, kidi, sogo, then the lead drum.
                if (c == 0) mark (out, at, "the timeline (gankogui)");
                if (c == 2) mark (out, at, "axatse enters");
                if (c == 3) mark (out, at, "kaganu enters");
                if (c == 4) mark (out, at, "kidi enters");
                if (c == 5) mark (out, at, "sogo enters");
                pattern (out.parts[0], at, pulse, bell, kit, 86);
                if (c >= 2)
                    pattern (out.parts[1], at, pulse, axatse, kit, 58);
                if (c >= 3)
                    pattern (out.parts[2], at, pulse, kaganu, kit, 72);
                if (c >= 4)
                    pattern (out.parts[3], at, pulse, kidi[(size_t) response], kit, 80);
                if (c >= 5)
                    pattern (out.parts[4], at, pulse, sogo[(size_t) response], kit, 84);
                if (c >= 6)
                {
                    // The lead: a call every few cycles that changes the responses; between, variations on a figure.
                    const bool calls = c == 6 || (c - 6) % 4 == 0;
                    if (calls)
                    {
                        mark (out, at, c == 6 ? "the lead drum's call" : "a call: the responses change");
                        if (c > 6)
                            response = 1 - response;
                        figure = below (r, (int) figures.size());
                        pattern (out.parts[5], at, pulse, call, kit, 100);
                    }
                    else
                    {
                        auto f = figures[(size_t) figure];
                        if (chance (r, 0.35))   // a variation: a stroke moved by a pulse
                        {
                            const auto i = (size_t) below (r, 12);
                            if (f[i] == '.')
                                f[i] = 'r';
                        }
                        pattern (out.parts[5], at, pulse, f, kit, 92);
                    }
                }
                // The song in call and response from the seventh cycle.
                if (c >= 7 && (c - 7) % 4 < 2 && (c - 7) % 2 == 0)
                {
                    const bool cantor = (c - 7) % 4 == 0;
                    int64_t x = at;
                    for (const auto& [d, len] : song)
                    {
                        add (out.parts[6], x, len * pulse, sc.key (d), 88);
                        add (out.parts[7], x + 24 * pulse, len * pulse, sc.key (d), 80);                 // the chorus answers the cantor's line
                        add (out.parts[7], x + 24 * pulse, len * pulse, sc.key (sc.stepDown (sc.stepDown (d))), 72);   // in thirds
                        x += len * pulse;
                    }
                    (void) cantor;
                }
            }
            // The closing call: the lead drum stops everyone on the bell's first stroke.
            const auto end = (int64_t) cycles * cycle;
            mark (out, end, "the closing call");
            pattern (out.parts[5], end, pulse, "AAA.A.A.....", kit, 110);
            for (int p = 0; p < 5; ++p)
                hit (out.parts[(size_t) p], end + 6 * pulse, p == 0 ? gm::lowAgogo : p == 4 ? gm::lowMidTom : p == 3 ? gm::openHiConga : p == 2 ? gm::highTimbale : gm::maracas, 110);
            out.end = end + cycle;
            out.parts.resize ((size_t) parts);
            out.summary = "The bell's twelve-pulse timeline under " + std::to_string (parts - 1) + " layers entering one by one, the lead drum's calls "
                          "changing the kidi and sogo's responses from three against four to two against three";
            return out;
        }

        //======================================================================
        // Hocket: a cyclic melody shared out, a pitch a player.
        Result hocket (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int players = std::clamp (s.parts, 2, 8);
            // Each player's pitch: the mode's degrees from high to low, one each.
            std::vector<int> pitch;
            for (int p = 0; p < players; ++p)
                pitch.push_back (sc.n() + 1 - p * std::max (1, (sc.n() + 2) * 2 / std::max (players, 1) / 2));
            // The melody: a cycle of 24 pulses, a player's pitch or a rest on each, never the same player twice running.
            const int period = 24;
            std::vector<int> melody (period, -1);
            int last = -1;
            for (int i = 0; i < period; ++i)
            {
                if (i % 3 != 2 && chance (r, 0.8))
                {
                    int p;
                    do
                        p = below (r, players);
                    while (p == last && players > 1);
                    melody[(size_t) i] = last = p;
                }
                else
                    last = -1;
            }
            const auto pulse = Q / 2;
            const auto cycle = (int64_t) period * pulse;
            const int cycles = std::max (6, (int) (std::max<int64_t> (s.bars, 8) * barTicks() / cycle));
            for (int c = 0; c < cycles; ++c)
            {
                const auto at = (int64_t) c * cycle;
                const bool varying = c >= cycles / 3 && c < cycles * 5 / 6;
                if (c == 0)
                    mark (out, at, "the melody, hocketed");
                else if (c == cycles / 3)
                    mark (out, at, "each figure varies (the melody stays)");
                else if (c == cycles * 5 / 6)
                    mark (out, at, "the melody again");
                // The players enter one by one in the first cycles, then all play.
                for (int i = 0; i < period; ++i)
                {
                    const auto p = melody[(size_t) i];
                    if (p < 0 || p >= players || (c < players && p > c + 1))
                        continue;
                    add (out.parts[(size_t) p], at + i * pulse, pulse, sc.key (pitch[(size_t) p]), i % 3 == 0 ? 96 : 82);
                }
                // A variation: a player adds its pitch in a rest of the melody next to one of its own notes.
                if (varying)
                    for (int i = 0; i < period; ++i)
                        if (melody[(size_t) i] < 0 && chance (r, 0.3))
                        {
                            const auto nb = melody[(size_t) ((i + 1) % period)] >= 0 ? melody[(size_t) ((i + 1) % period)] : melody[(size_t) ((i + period - 1) % period)];
                            if (nb >= 0)
                                add (out.parts[(size_t) nb], at + i * pulse, pulse, sc.key (pitch[(size_t) nb]), 70);
                        }
            }
            out.end = (int64_t) cycles * cycle;
            out.parts.resize ((size_t) players);
            out.summary = "A cycle of " + std::to_string (period) + " pulses shared out among " + std::to_string (players) + " players, one pitch each, interlocking";
            return out;
        }
    }

    //==========================================================================
    Result generate (const Settings& settingsIn)
    {
        auto s = settingsIn;
        const auto& inf = info (s.method);
        s.parts = std::clamp (s.parts, 1, std::min (inf.mostParts, maxParts));
        s.bars = std::clamp (s.bars, 4, 512);
        Random r (s.seed * 2654435761u + (uint32_t) s.method);
        Result out;
        switch (s.method)
        {
            case Method::organum:      out = organum (s, r); break;
            case Method::isorhythm:    out = isorhythm (s, r); break;
            case Method::counterpoint: out = speciesStudy (s, r); break;
            case Method::fugue:        out = fugueOnSubject (s, r); break;
            case Method::stochastic:   out = stochasticMatrix (s, r); break;
            case Method::aleatory:     out = aleatory (s, r); break;
            case Method::process:      out = process (s, r); break;
            case Method::tintinnabuli: out = tintinnabuli (s, r); break;
            case Method::spectral:     out = spectral (s, r); break;
            case Method::raga:         out = raga (s, r); break;
            case Method::maqam:        out = maqam (s, r); break;
            case Method::radif:        out = radif (s, r); break;
            case Method::gamelan:      out = gamelan (s, r); break;
            case Method::joHaKyu:      out = joHaKyu (s, r); break;
            case Method::sanjo:        out = sanjo (s, r); break;
            case Method::chineseSuite: out = chineseSuite (s, r); break;
            case Method::ostinato:     out = ostinato (s, r); break;
            case Method::hocket:       out = hocket (s, r); break;
            case Method::silence:      out = silence (s, r); break;
            case Method::starCharts:   out = starCharts (s, r); break;
            case Method::musicircus:   out = musicircus (s, r); break;
            case Method::timeBrackets: out = timeBrackets (s, r); break;
            case Method::tuningMeditation: out = tuningMeditation (s, r); break;
            case Method::deepListening: out = deepListening (s, r); break;
        }
        if ((int) out.parts.size() > maxParts)
            out.parts.resize ((size_t) maxParts);
        for (auto& p : out.parts)
        {
            std::stable_sort (p.notes.begin(), p.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            for (const auto& n : p.notes)
                out.end = std::max (out.end, n.end);
        }
        out.bpm = s.bpm;
        if (out.sections.empty() || out.sections.front().start != 0)
            out.sections.insert (out.sections.begin(), { 0, "start" });
        return out;
    }

    midifile::SongPtr toSong (const Result& result, const std::string& title)
    {
        std::vector<midifile::Event> events;
        uint16_t channels = 0;
        int nextChannel = 0;
        for (const auto& part : result.parts)
        {
            int channel = 9;
            if (! part.drums)
            {
                if (nextChannel == 9)
                    ++nextChannel;
                channel = std::min (nextChannel++, 15);
            }
            const bool bends = std::any_of (part.notes.begin(), part.notes.end(), [] (const Note& n) { return std::abs (n.cents) > 0.5; });
            for (const auto& n : part.notes)
            {
                if (bends && ! part.drums)   // ±2 semitones, the editor's and every synth's default range
                {
                    const auto value = std::clamp ((int) std::lround (8192.0 + n.cents / 200.0 * 8192.0), 0, 16383);
                    events.push_back ({ n.start, (uint8_t) (0xE0 | channel), (uint8_t) (value & 0x7F), (uint8_t) (value >> 7) });
                }
                events.push_back ({ n.start, (uint8_t) (0x90 | channel), (uint8_t) n.key, (uint8_t) n.velocity });
                events.push_back ({ n.end, (uint8_t) (0x80 | channel), (uint8_t) n.key, 0 });
            }
            if (! part.notes.empty())
                channels |= (uint16_t) (1u << channel);
        }
        std::stable_sort (events.begin(), events.end(), [] (const midifile::Event& a, const midifile::Event& b)
        {
            // Offs before bends before ons at the same tick, so a note struck again is not cut and each bend is its note's.
            auto rank = [] (const midifile::Event& e) { return e.kind() == 0x80 ? 0 : e.kind() == 0xE0 ? 1 : 2; };
            return a.tick != b.tick ? a.tick < b.tick : rank (a) < rank (b);
        });
        auto out = std::make_shared<midifile::Song>();
        out->format = 0;
        out->ticksPerQuarter = ticksPerQuarter;
        out->tempos.push_back ({ 0, 60.0e6 / std::max (1.0, result.bpm) });
        out->trackNames.push_back (title);
        out->events = std::move (events);
        for (const auto& x : result.sections)
            out->markers.push_back ({ x.start, x.name });
        out->channelsUsed = channels;
        out->lastTick = std::max (result.end, out->events.empty() ? 0 : out->events.back().tick);
        return out;
    }
}
