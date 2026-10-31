// SPDX-License-Identifier: AGPL-3.0-only
#include "Compose.h"
#include "Fugue.h"
#include "Minimalize.h"
#include "Stochastic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
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
            { Method::bolGrammar, "bolgrammar", "Bol Processor Grammar", "Tablā qāʿida (Lucknow), after the Bol Processor", "south and west asia",
              "A tablā qāʿida in tīntāl derived by a weighted grammar, as Bel and Kippen's Bol Processor models it: a theme of bols, then "
              "variations rewritten from the theme's own cells, each played bharī (open) and then khālī (the bāyāṅ's resonance taken "
              "away, dha to ta, ge to ke) returning to open for sam, and a tihāī; the lehrā's nagma and the tānpūrā beneath.",
              "Kippen and Bel, \"Modelling music with grammars: formal language representation in the Bol Processor\" (1992); "
              "Kippen, The Tabla of Lucknow (1988); Bel, Bol Processor (bolprocessor.org)", "",
              3, 3, "tablā|lehrā (sāraṅgī)|tānpūrā" },
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
            { Method::interlocking, "interlocking", "Interlocking Polyphony", "Mbuti and Aka singing (Central African rainforest)", "africa",
              "Each voice sings its own short figure over a common pulse, some yodelled between chest and head voice, the figures of "
              "different lengths in one period; they overlap and fill each other's gaps, so the melody is in the composite and no one "
              "sings it. Voices enter one by one, each varies its figure without breaking the whole, and the leader closes alone.",
              "Arom, African Polyphony and Polyrhythm (1991); Fürniss on Aka yodel and polyphony; Turnbull and Chapman, Mbuti Pygmies "
              "of the Ituri Rainforest (Smithsonian Folkways, notes ed. Kisliuk); Kisliuk, Seize the Dance! (1998)", "",
              5, 8, "leader (yodel)|voice 2 (yodel)|voice 3|voice 4|voice 5 (low)|voice 6|voice 7 (yodel)|hand claps" },
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
            constexpr int kick = 36, acousticKick = 35, rim = 37, snare = 38, handClap = 39, lowFloorTom = 41, lowTom = 45, lowMidTom = 47,
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

        //======================================================================
        // Interlocking polyphony (Mbuti, Aka): not one melody shared out, as hocket() does, but each
        // voice its own short figure, of its own length within one period, placed in the others' gaps;
        // the melody is what they make together. Some voices yodel, leaping between a chest note and a
        // head note. An abstraction of the principle (Arom's, Fürniss's), not a transcription.
        Result interlocking (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 8);
            const int voices = std::min (parts, 7);   // the eighth part claps
            if (parts == 8)
                out.parts[7].drums = true;
            // The pitches: the mode's degrees nearest an anhemitonic pentatonic, in every period.
            std::vector<int> penta;
            for (double c : { 0.0, 200.0, 400.0, 700.0, 900.0 })
            {
                const auto d = sc.index (sc.nearest (c));
                if (std::find (penta.begin(), penta.end(), d) == penta.end())
                    penta.push_back (d);
            }
            std::sort (penta.begin(), penta.end());
            const int k = (int) penta.size();
            auto tone = [&] (int i) { return penta[(size_t) (i - floorDiv (i, k) * k)] + floorDiv (i, k) * sc.n(); };

            // The voices: where each sings (an index into the pentatonic), whether it yodels, how long its figure is.
            constexpr int period = 24;
            const std::array<int, 7> base { k, 2, 1, 0, -k, 2, k - 1 };
            const std::array<bool, 7> yodels { true, true, false, false, false, false, true };
            const std::array<int, 7> lengths { 24, 12, 8, 6, 12, 8, 6 };
            struct Figure
            {
                int length = period;
                std::vector<std::pair<int, int>> notes;   // pulse in the figure, pentatonic index
            };
            std::vector<Figure> figures ((size_t) voices);
            std::array<int, period> heard {};   // how many voices already sound on each pulse of the period
            auto occupy = [&] (const Figure& f, int sign)
            {
                for (const auto& note : f.notes)
                    for (int x = note.first; x < period; x += f.length)
                        heard[(size_t) x] += sign;
            };
            // The emptiest pulse of a figure's length: the fewest voices there in all its repetitions.
            auto emptiest = [&] (const Figure& f, const std::vector<int>& taken)
            {
                int best = -1;
                double bestScore = 1e9;
                for (int j = 0; j < f.length; ++j)
                {
                    if (std::find (taken.begin(), taken.end(), j) != taken.end())
                        continue;
                    double score = r.uniform() * 0.8;
                    for (int x = j; x < period; x += f.length)
                        score += heard[(size_t) x] * (double) f.length / period;
                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = j;
                    }
                }
                return best;
            };
            for (int v = 0; v < voices; ++v)
            {
                auto& f = figures[(size_t) v];
                f.length = lengths[(size_t) v];
                const int count = v == 0 ? between (r, 8, 10) : yodels[(size_t) v] ? std::max (2, f.length / 3) : std::max (2, f.length / 4 + below (r, 2));
                std::vector<int> at;
                for (int i = 0; i < count; ++i)
                    if (const auto j = emptiest (f, at); j >= 0)
                        at.push_back (j);
                std::sort (at.begin(), at.end());
                int t = base[(size_t) v], dir = 0;
                bool high = chance (r, 0.5);
                for (size_t i = 0; i < at.size(); ++i)
                {
                    if (yodels[(size_t) v])
                    {
                        // The yodel: chest and head voice by turns, a sixth to an octave apart.
                        t = high ? base[(size_t) v] + between (r, 3, 4) : base[(size_t) v] + below (r, 2) * (chance (r, 0.5) ? 1 : -1);
                        high = ! high;
                    }
                    else if (i > 0)
                    {
                        const int step = chance (r, 0.15) ? 0 : chance (r, 0.25) ? 2 : 1;
                        dir = std::abs (t + step - base[(size_t) v]) > 2 ? -1 : std::abs (t - step - base[(size_t) v]) > 2 ? 1 : chance (r, 0.5) ? 1 : -1;
                        t += dir * step;
                    }
                    f.notes.push_back ({ at[i], t });
                }
                occupy (f, 1);
            }

            const auto pulse = Q / 2;
            const auto cycle = (int64_t) period * pulse;
            const int cycles = std::max (voices + 4, (int) (std::max<int64_t> (s.bars, 8) * barTicks() / cycle));
            const int full = voices;                  // the cycle where every voice sings
            const int thin = cycles - 2;              // where they leave
            auto sing = [&] (int v, int64_t at, int velocityDrop)
            {
                const auto& f = figures[(size_t) v];
                for (int rep = 0; rep * f.length < period; ++rep)
                    for (size_t i = 0; i < f.notes.size(); ++i)
                    {
                        const auto [p, t] = f.notes[i];
                        const auto next = i + 1 < f.notes.size() ? f.notes[i + 1].first : f.notes.front().first + f.length;
                        const auto len = std::clamp<int64_t> (next - p, 1, yodels[(size_t) v] ? 1 : 2) * pulse;
                        const bool head = yodels[(size_t) v] && t >= base[(size_t) v] + 3;
                        add (out.parts[(size_t) v], at + (int64_t) (rep * f.length + p) * pulse, len, sc.key (tone (t)),
                             (head ? 76 : 90) - velocityDrop + (p == 0 ? 8 : 0));
                    }
            };
            for (int c = 0; c < cycles; ++c)
            {
                const auto at = (int64_t) c * cycle;
                if (c == 0)
                    mark (out, at, "the leader's figure");
                else if (c < full)
                    mark (out, at, std::string (split (info (s.method).parts)[(size_t) c]) + " enters");
                else if (c == full)
                    mark (out, at, "all the voices interlock");
                else if (c == full + 1 && full + 1 < thin)
                    mark (out, at, "each voice varies its figure");
                else if (c == thin)
                    mark (out, at, "the voices leave");
                // Each voice's own variation, now and then, kept: a note moved to an empty pulse, or sung a step away.
                if (c > full && c < thin)
                    for (int v = 1; v < voices; ++v)
                        if (chance (r, 0.3))
                        {
                            auto& f = figures[(size_t) v];
                            occupy (f, -1);
                            const auto i = (size_t) below (r, (int) f.notes.size());
                            if (yodels[(size_t) v] || chance (r, 0.5))
                            {
                                std::vector<int> taken;
                                for (const auto& n : f.notes)
                                    taken.push_back (n.first);
                                if (const auto j = emptiest (f, taken); j >= 0)
                                    f.notes[i].first = j;
                                std::sort (f.notes.begin(), f.notes.end());
                            }
                            else
                                f.notes[i].second += chance (r, 0.5) ? 1 : -1;
                            occupy (f, 1);
                        }
                for (int v = 0; v < voices; ++v)
                {
                    const bool in = c >= v && (c < thin || v == 0 || (c == thin && v < (voices + 1) / 2));
                    if (in)
                        sing (v, at, c >= thin ? 8 : 0);
                }
                if (parts == 8 && c >= 2 && c < thin)   // the claps on every third pulse, the period's first accented
                    for (int x = 0; x < period; x += 3)
                        hit (out.parts[7], at + x * pulse, gm::handClap, x == 0 ? 96 : 74);
            }
            const auto end = (int64_t) cycles * cycle;
            mark (out, end, "the leader closes");
            add (out.parts[0], end, 4 * pulse, sc.key (tone (k)), 84);
            out.end = end + 4 * pulse;
            out.parts.resize ((size_t) parts);
            out.summary = std::to_string (voices) + (voices == 1 ? " voice" : " voices") + " in a period of " + std::to_string (period)
                          + " pulses, each its own figure of 6 to 24 pulses in the others' gaps (" + std::to_string (std::count (yodels.begin(), yodels.begin() + voices, true))
                          + " yodelling), so the melody is in the composite";
            return out;
        }

        //======================================================================
        // A weighted grammar as the Bol Processor writes one: "X -> <w> a b C | <w-d> d E". A word
        // beginning with a capital is a variable, the rest are terminals (bols, '-' a rest). <w> is
        // an alternative's weight, <w-d> a decremental weight that drops by d each time it is used
        // (so a grammar used again tries its other alternatives). derive() rewrites the leftmost
        // variable this grammar has rules for until none is left; variables it has no rules for stay,
        // for the next subgrammar. Our own code after Bel and Kippen's descriptions, not BP's.
        struct Grammar
        {
            struct Alternative
            {
                std::vector<std::string> words;
                double weight = 1.0, decrement = 0.0;
            };
            std::map<std::string, std::vector<Alternative>> rules;

            static std::vector<std::string> words (const std::string& text)
            {
                std::vector<std::string> out;
                std::string w;
                for (char c : text + " ")
                    if (std::isspace ((unsigned char) c))
                    {
                        if (! w.empty())
                            out.push_back (w);
                        w.clear();
                    }
                    else
                        w += c;
                return out;
            }

            void rule (const std::string& text)
            {
                const auto arrow = text.find ("->");
                const auto left = words (text.substr (0, arrow));
                if (arrow == std::string::npos || left.size() != 1)
                    return;
                auto& alternatives = rules[left[0]];
                std::string rest = text.substr (arrow + 2) + "|";
                for (size_t from = 0, bar; (bar = rest.find ('|', from)) != std::string::npos; from = bar + 1)
                {
                    Alternative a;
                    a.words = words (rest.substr (from, bar - from));
                    if (! a.words.empty() && a.words[0].front() == '<' && a.words[0].back() == '>')
                    {
                        const auto w = a.words[0].substr (1, a.words[0].size() - 2);
                        const auto minus = w.find ('-');
                        a.weight = std::atof (w.substr (0, minus).c_str());
                        if (minus != std::string::npos)
                            a.decrement = std::atof (w.substr (minus + 1).c_str());
                        a.words.erase (a.words.begin());
                    }
                    alternatives.push_back (a);
                }
            }

            std::vector<std::string> derive (std::vector<std::string> s, Random& r)
            {
                for (int guard = 0; guard < 4000; ++guard)
                {
                    auto it = std::find_if (s.begin(), s.end(), [this] (const std::string& w)
                                            { return std::isupper ((unsigned char) w[0]) && rules.count (w) > 0; });
                    if (it == s.end())
                        break;
                    auto& alternatives = rules[*it];
                    double total = 0.0;
                    for (const auto& a : alternatives)
                        total += std::max (0.0, a.weight);
                    size_t pick = 0;
                    if (total <= 0.0)
                        pick = (size_t) below (r, (int) alternatives.size());
                    else
                        for (double x = r.uniform() * total; pick + 1 < alternatives.size(); ++pick)
                            if ((x -= std::max (0.0, alternatives[pick].weight)) < 0.0)
                                break;
                    auto& a = alternatives[pick];
                    a.weight = std::max (0.0, a.weight - a.decrement);
                    const auto at = it - s.begin();
                    s.erase (it);
                    s.insert (s.begin() + at, a.words.begin(), a.words.end());
                }
                return s;
            }
        };

        std::string joined (const std::vector<std::string>& w)
        {
            std::string out;
            for (const auto& x : w)
                out += (out.empty() ? "" : " ") + x;
            return out;
        }

        // A tablā qāʿida derived by grammars, after Kippen and Bel's Bol Processor work on the Lucknow
        // tablā: subgrammar 1 makes the theme's cells, 2 its form, 3 each variation from the theme's
        // own cells by position, and 4 the tihāī; then each is played bharī and khālī.
        Result bolGrammar (const Settings& s, Random& r)
        {
            Scale sc (s.mode);
            auto out = begin (s);
            const int parts = std::clamp (s.parts, 1, 3);
            out.parts[0].drums = true;

            // 1. The cells, a beat of four bols each; K the cadence (the theme's refrain).
            Grammar cells;
            cells.rule ("A -> <30> dha ti dha ge | <20> dha dha ti ra | <20> dha - ti ra | <15> dha ge na dha | <15> dha ti ra ki");
            cells.rule ("B -> <30> na dha ti ra | <25> ki ta dha ge | <20> ti ra ki ta | <15> dha ti dha ge");
            cells.rule ("C -> <30> ge na dha ge | <25> dha ge tin na | <20> ti ra ki ta | <15> dha - dha ge");
            cells.rule ("D -> <30> dha ge dhin na | <25> ti ra ki ta | <20> na dha ti ra");
            cells.rule ("K -> <40> dhin na ge na | <30> dha ge dhin na | <30> dhi na ge na");
            const std::array<const char*, 5> letters { "A", "B", "C", "D", "K" };
            std::map<std::string, std::vector<std::string>> cell;
            for (const auto* l : letters)
                cell[l] = cells.derive ({ l }, r);

            // 2. The theme: eight cells, the cadence last.
            Grammar form;
            form.rule ("THEME -> <40> A B A C A B D K | <30> A A B C A A D K | <30> A B C B A B D K");
            const auto theme = form.derive ({ "THEME" }, r);

            // 3. The variations: the theme's cells by position (P1..P8), a new first half and the theme's
            // second half as refrain, or two new halves and the whole theme.
            Grammar variation;
            variation.rule ("VAR -> <60-12> HALF P5 P6 P7 P8 | <40> HALF HALF P1 P2 P3 P4 P5 P6 P7 P8");
            variation.rule ("HALF -> <20-6> P1 P1 P2 P3 | <20-6> P1 P2 P1 P2 | <15-6> P3 P4 P1 P2 | <15-6> P1 P2 P3 P3 | <15-6> P2 P2 P1 P2 "
                            "| <10-5> P1 P1 P1 P2 | <10-5> P3 P3 P1 P2 | <10-5> P2 P3 P2 P3 | <10-5> P1 P3 P2 P4 | <10-5> P4 P4 P1 P2");
            auto letter = [&] (const std::string& p) { return theme[(size_t) std::clamp (std::atoi (p.c_str() + 1) - 1, 0, 7)]; };
            auto bols = [&] (const std::vector<std::string>& positions)
            {
                std::vector<std::string> out2;
                for (const auto& p : positions)
                {
                    const auto& c = cell[p[0] == 'P' ? letter (p) : p];
                    out2.insert (out2.end(), c.begin(), c.end());
                }
                return out2;
            };
            auto named = [&] (const std::vector<std::string>& positions)
            {
                std::vector<std::string> l;
                for (const auto& p : positions)
                    l.push_back (p[0] == 'P' ? letter (p) : p);
                return joined (l);
            };

            // The strokes: the dāyāṅ (right) on the high congas, the bāyāṅ (left) open on the low conga, closed on the rim.
            const std::map<std::string, std::vector<int>> strokes {
                { "dha", { gm::openHiConga, gm::lowConga } }, { "dhin", { gm::muteHiConga, gm::lowConga } }, { "dhi", { gm::muteHiConga, gm::lowConga } },
                { "na", { gm::openHiConga } }, { "ta", { gm::openHiConga } }, { "tin", { gm::muteHiConga } }, { "ti", { gm::muteHiConga } },
                { "ra", { gm::muteHiConga } }, { "tu", { gm::highTimbale } }, { "ge", { gm::lowConga } }, { "ke", { gm::rim } },
                { "ki", { gm::rim } }, { "ka", { gm::rim } } };
            // Khālī: the bāyāṅ's open resonance taken away.
            const std::map<std::string, std::string> closed { { "dha", "ta" }, { "dhin", "tin" }, { "dhi", "ti" }, { "ge", "ke" } };
            const auto unit = Q / 4;   // four bols a beat
            const auto cycleLen = 64 * unit;   // tīntāl, sixteen beats
            auto play = [&] (const std::vector<std::string>& b, int64_t at, bool khali, size_t openFrom)
            {
                for (size_t i = 0; i < b.size(); ++i)
                {
                    auto bol = b[i];
                    if (khali && i < openFrom)
                        if (const auto c = closed.find (bol); c != closed.end())
                            bol = c->second;
                    const auto t = at + (int64_t) i * unit;
                    const auto st = strokes.find (bol);
                    if (st == strokes.end())
                        continue;
                    for (auto drum : st->second)
                        hit (out.parts[0], t, drum, t % cycleLen == 0 ? 112 : i % 4 == 0 ? 92 : bol == "ra" ? 62 : 78, unit);
                }
                return at + (int64_t) b.size() * unit;
            };
            // A statement: bharī, then khālī, the last two cells open again to lead back to sam.
            auto state = [&] (const std::vector<std::string>& positions, int64_t at)
            {
                const auto b = bols (positions);
                at = play (b, at, false, 0);
                return play (b, at, true, b.size() >= 8 ? b.size() - 8 : 0);
            };

            const int cycles = std::max (4, s.bars / 4);
            int64_t t = 0;
            mark (out, t, "theme: " + joined (theme));
            t = state ({ "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8" }, t);
            std::vector<std::string> done;
            int count = 0;
            while (t + cycleLen < (int64_t) cycles * cycleLen)
            {
                const auto room = (int64_t) cycles * cycleLen - cycleLen - t;   // the last cycle is the tihāī's
                std::vector<std::string> v;
                bool found = false;
                for (int attempt = 0; attempt < 24 && ! found; ++attempt)
                {
                    v = variation.derive ({ "VAR" }, r);
                    const auto fits = (int64_t) v.size() * 4 * unit * 2 <= room;
                    found = fits && (std::find (done.begin(), done.end(), named (v)) == done.end() || attempt > 16);
                    if (! fits)
                        v.clear();
                }
                if (v.empty())
                {
                    // The grammar's shortest: a new half and the refrain.
                    v = Grammar::words ("P1 P1 P2 P3 P5 P6 P7 P8");
                    if ((int64_t) v.size() * 4 * unit * 2 > room)
                        break;
                }
                done.push_back (named (v));
                mark (out, t, "variation " + std::to_string (++count) + ": " + named (v));
                t = state (v, t);
            }
            // 4. The tihāī's cycle: the theme bharī on beats 1 to 8, then its last two cells and a dha three
            // times from beat 9, three bols' rest between: 33 bols, the last dha on the next sam.
            Grammar tihai;
            tihai.rule ("TIHAI -> PHRASE - - - PHRASE - - - PHRASE");
            tihai.rule ("PHRASE -> P7 P8 dha");
            mark (out, t, "tihāī");
            t = play (bols ({ "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8" }), t, false, 0);
            std::vector<std::string> ti;
            for (const auto& w : tihai.derive ({ "TIHAI" }, r))
                if (w[0] == 'P')
                    for (const auto& b : cell[letter (w)])
                        ti.push_back (b);
                else
                    ti.push_back (w);
            play (ti, t, false, 0);
            const auto sam = t + (int64_t) ti.size() * unit - unit;
            const auto end = sam + 4 * Q;
            mark (out, sam, "sam");

            // The lehrā: a nagma of one cycle, the same each time, on Sa at sam; the tānpūrā beneath.
            std::vector<std::pair<int, int>> nagma;   // degree, eighths
            {
                int d = 0, dir = 0, eighths = 0;
                nagma.push_back ({ 0, 2 });
                eighths = 2;
                while (eighths < 32)
                {
                    const int len = std::min (32 - eighths, chance (r, 0.6) ? 1 : 2);
                    d = walk (sc, d, -2, sc.n() + 1, r, dir, 0.15);
                    if (eighths >= 28)   // home toward Sa
                        d = d > 0 ? sc.stepDown (d) : d < 0 ? sc.stepUp (d) : d;
                    nagma.push_back ({ d, len });
                    eighths += len;
                }
            }
            const int n = sc.n();
            for (int64_t at = 0; at < sam; at += cycleLen)
            {
                if (parts >= 2)
                {
                    int64_t x = at;
                    for (const auto& [d, len] : nagma)
                    {
                        add (out.parts[1], x, len * Q / 2, sc.key (d), x == at ? 84 : 70);
                        x += len * Q / 2;
                    }
                }
                if (parts >= 3)
                    for (int b = 0; b < 16; ++b)
                        add (out.parts[2], at + b * Q, Q, sc.key (std::array<int, 4> { sc.fifth() - n, 0, 0, -n }[(size_t) (b % 4)]), 54);
            }
            for (auto drum : strokes.at ("dha"))
                hit (out.parts[0], sam, drum, 120, unit);
            if (parts >= 2)
                add (out.parts[1], sam, 4 * Q, sc.key (0), 90);
            if (parts >= 3)
                add (out.parts[2], sam, 4 * Q, sc.key (-n), 60);
            out.end = end;
            out.parts.resize ((size_t) parts);
            std::string legend;
            for (const auto* l : { "A", "B", "C", "D", "K" })
                if (std::find (theme.begin(), theme.end(), l) != theme.end())
                    legend += std::string (legend.empty() ? "" : ", ") + l + " = " + joined (cell[l]);
            out.summary = "A qāʿida in tīntāl derived by grammars: the theme " + joined (theme) + " (" + legend + "), " + std::to_string (count)
                          + " variations from its cells, each bharī then khālī, and a tihāī landing on sam";
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
            case Method::interlocking: out = interlocking (s, r); break;
            case Method::bolGrammar:   out = bolGrammar (s, r); break;
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
