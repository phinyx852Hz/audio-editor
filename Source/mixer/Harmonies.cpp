// SPDX-License-Identifier: AGPL-3.0-only
#include "Harmonies.h"
#include "Tunings.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace harmony
{
    namespace
    {
        // A voice from text: "pitch:beats" a note, "-:beats" a rest, | only to read it by.
        Voice line (std::string name, const std::string& text)
        {
            Voice v { std::move (name), {} };
            std::istringstream in (text);
            std::string token;
            while (in >> token)
            {
                if (token == "|")
                    continue;
                const auto colon = token.find (':');
                Note n;
                n.beats = std::stod (token.substr (colon + 1));
                if (token[0] == '-' && colon == 1)
                    n.rest = true;
                else
                    n.pitch = std::stoi (token.substr (0, colon));
                v.notes.push_back (n);
            }
            return v;
        }

        // Voices from chords: each chord's notes from the bottom up, the voice a chord's
        // position, held for its beats. Every chord has as many notes as there are names.
        struct Chord { std::vector<int> notes; double beats; };
        std::vector<Voice> chords (const std::vector<std::string>& names, const std::vector<Chord>& list)
        {
            std::vector<Voice> out;
            for (size_t i = 0; i < names.size(); ++i)
            {
                Voice v { names[i], {} };
                for (const auto& c : list)
                    v.notes.push_back ({ c.notes[i], c.beats, false });
                out.push_back (std::move (v));
            }
            return out;
        }

        // The same chord shape on each root.
        std::vector<Chord> planed (const std::vector<int>& shape, const std::vector<std::pair<int, double>>& roots)
        {
            std::vector<Chord> out;
            for (const auto& [r, b] : roots)
            {
                Chord c { {}, b };
                for (auto s : shape)
                    c.notes.push_back (r + s);
                out.push_back (c);
            }
            return out;
        }

        using P = Part;

        std::vector<Harmony> build()
        {
            std::vector<Harmony> h;
            auto add = [&h] (Harmony x) { h.push_back (std::move (x)); };
            const std::vector<std::string> satb { "bass", "tenor", "alto", "soprano" };

            // ---- Western art music, by era ----
            add ({ "parallel organum", "parallel organum", P::western, "middle ages", "Musica enchiriadis", "c. 900", 900, "parallel",
                   "The first written polyphony: a chant (vox principalis) doubled a fourth or fifth below by the vox organalis, "
                   "the doubling bent at the edges of the phrase to avoid the tritone, and both voices may be doubled at the octave.",
                   "perfect fourths, fifths and octaves in parallel; unison at the start and end",
                   "Musica enchiriadis and Scolica enchiriadis (c. 900); Grout & Palisca", "pythagorean", 60.0,
                   { line ("vox principalis", "7:1 9:1 10:1 12:2 10:1 9:1 7:2"), line ("vox organalis", "2:1 4:1 5:1 7:2 5:1 4:1 2:2") } });
            add ({ "organum purum", "organum purum", P::western, "middle ages", "Notre-Dame school", "1160s–1250", 1160, "drone",
                   "Léonin's two-voice organum for the Magnus liber organi: the chant's notes stretched into a long held tenor, over "
                   "which the duplum spins melismas, meeting it on fifths, octaves and unisons and passing through dissonance between.",
                   "perfect consonances on the tenor's notes; seconds, thirds and sixths passing",
                   "Magnus liber organi; Anonymous IV (c. 1275); Roesner, Le Magnus liber organi de Notre-Dame", "pythagorean", 60.0,
                   { line ("tenor", "0:8"), line ("duplum", "7:1 9:0.5 10:0.5 12:2 10:1 9:1 7:2") } });
            add ({ "english discant and gymel", "english discant", P::western, "middle ages", "England", "13th–15th c.", 1250, "parallel",
                   "The English taste for imperfect consonances: two voices moving in thirds (gymel, 'twin song') or three in chains of "
                   "sixth-chords, heard on the Continent as the contenance angloise of Dunstable.",
                   "parallel thirds and sixths, cadencing to the fifth and octave",
                   "Sumer is icumen in (13th c.); Bukofzer, Studies in Medieval and Renaissance Music; Martin le Franc, Le champion des dames", "", 66.0,
                   chords ({ "low", "middle", "high" }, { { { 0, 4, 9 }, 1 }, { { 2, 5, 11 }, 1 }, { { 4, 7, 12 }, 1 }, { { 2, 5, 11 }, 1 },
                                                         { { 0, 4, 9 }, 1 }, { { -1, 2, 7 }, 1 }, { { 0, 7, 12 }, 2 } }) });
            add ({ "fauxbourdon", "fauxbourdon", P::western, "renaissance", "Guillaume Du Fay (1397–1474)", "c. 1430", 1430, "parallel",
                   "A chant in the top voice, a tenor a sixth below it, and an unwritten middle voice sung a fourth below the top: "
                   "chains of 6/3 chords that open out to an octave and fifth at the cadence.",
                   "6/3 chords in parallel; the double leading-tone cadence to 8/5",
                   "Du Fay, Missa Sancti Jacobi (c. 1427); Trowell, 'Faburden and fauxbourdon'", "", 66.0,
                   { line ("tenor", "7:1 5:1 4:1 2:1 0:2"), line ("fauxbourdon", "11:1 9:1 7:1 6:1 7:2"), line ("cantus", "16:1 14:1 12:1 11:1 12:2") } });
            add ({ "modal counterpoint", "modal counterpoint", P::western, "renaissance", "Giovanni Pierluigi da Palestrina (1525–1594)", "16th c.", 1525, "counterpoint",
                   "Independent voices in a church mode, every dissonance prepared and resolved: the passing tone on the weak beat, the "
                   "suspension on the strong one, the cadence on the octave or a full triad. Codified by Zarlino and, later, Fux's species.",
                   "triads and their inversions; the 4–3 and 7–6 suspension; no parallel fifths or octaves",
                   "Zarlino, Le istitutioni harmoniche (1558); Fux, Gradus ad Parnassum (1725); Jeppesen, Counterpoint", "", 60.0,
                   { line ("bass", "-12:2 -7:2 -5:2 -12:2"), line ("tenor", "4:2 5:2 2:2 4:2"), line ("alto", "7:2 9:2 7:2 7:2"),
                     line ("soprano", "12:2 12:2 12:1 11:1 12:2") } });
            add ({ "basso continuo and figured bass", "basso continuo", P::western, "baroque", "Italy", "c. 1600–1750", 1600, "chordal",
                   "A bass line with figures, realized at sight by keyboard or lute as chords above it: harmony thought of from the bass "
                   "up, the seed of functional harmony and of the chord as a thing in itself.",
                   "5/3 and 6/3 chords, 6/4 and 7 by figure; the cadential 6/4–5/3",
                   "Viadana, Cento concerti ecclesiastici (1602); Heinichen, Der General-Bass (1728); Arnold, The Art of Accompaniment from a Thorough-Bass", "", 80.0,
                   chords (satb, { { { -12, 4, 7, 12 }, 1 }, { { -8, 4, 7, 12 }, 1 }, { { -7, 5, 9, 12 }, 1 }, { { -5, 7, 12, 16 }, 0.5 },
                                   { { -5, 5, 11, 14 }, 0.5 }, { { -12, 4, 7, 12 }, 2 } }) });
            add ({ "functional harmony", "functional harmony", P::western, "common practice", "Jean-Philippe Rameau (1683–1764)", "1722", 1683, "functional",
                   "Major and minor keys in which every chord has a function, tonic, subdominant or dominant, and the fundamental bass "
                   "moves by fifths toward the tonic: the harmony of Bach's chorales, Haydn, Mozart and Beethoven.",
                   "triads and the dominant seventh; I–IV–V–I; the perfect and the deceptive cadence",
                   "Rameau, Traité de l'harmonie (1722); Riemann, Vereinfachte Harmonielehre (1893); Aldwell & Schachter, Harmony and Voice Leading", "", 72.0,
                   chords (satb, { { { -12, 4, 7, 12 }, 2 }, { { -7, 5, 9, 12 }, 2 }, { { -5, 5, 7, 11 }, 2 }, { { -12, 4, 7, 12 }, 2 } }) });
            add ({ "romantic chromaticism", "romantic chromaticism", P::western, "common practice", "Richard Wagner (1813–1883)", "1859", 1813, "functional",
                   "Keys stretched by chromatic voice leading until the tonic is implied more than stated: augmented sixths, "
                   "half-diminished sevenths that do not resolve, and the Tristan chord.",
                   "the Tristan chord (F–B–D♯–G♯), the augmented sixth, the diminished seventh, chromatic mediants",
                   "Wagner, Tristan und Isolde (1859); Kurth, Romantische Harmonik (1920); Bailey (ed.), Prelude and Transfiguration", "", 50.0,
                   { line ("bass", "-7:3 -8:1 -7:4"), line ("tenor", "-1:3 -2:1 -2:4"), line ("alto", "3:3 2:1 4:4"),
                     line ("soprano", "8:2 8:1 9:1 10:4") } });
            add ({ "neo-riemannian transformations", "neo-riemannian", P::western, "common practice", "Franz Liszt (1811–1886)", "19th c.", 1811, "chordal",
                   "Triads joined not by function but by smooth voice leading: P (parallel), L (leading-tone exchange) and R (relative) "
                   "each move one note, so chains like the hexatonic cycle drift through distant keys. Named after Hugo Riemann by Cohn and Lewin.",
                   "major and minor triads a third apart; the hexatonic and octatonic cycles",
                   "Cohn, 'Introduction to Neo-Riemannian Theory' (1998); Lewin, Generalized Musical Intervals and Transformations (1987)", "", 60.0,
                   chords ({ "low", "middle", "high" }, { { { 0, 4, 7 }, 2 }, { { 0, 3, 7 }, 2 }, { { -1, 3, 8 }, 2 }, { { -1, 4, 8 }, 2 },
                                                         { { -1, 4, 7 }, 2 }, { { 0, 4, 7 }, 2 } }) });
            add ({ "impressionist planing", "planing", P::western, "twentieth century", "Claude Debussy (1862–1918)", "c. 1890–1915", 1862, "parallel",
                   "Chords moved in parallel as a single colour, the dominant ninth that does not resolve, whole-tone and pentatonic "
                   "fields: harmony as timbre rather than progression.",
                   "parallel ninth chords; whole-tone and pentatonic sonorities",
                   "Debussy, La cathédrale engloutie, Nuages; Lockspeiser, Debussy; DeVoto, Debussy and the Veil of Tonality", "", 60.0,
                   chords ({ "root", "third", "fifth", "seventh", "ninth" }, planed ({ 0, 4, 7, 10, 14 }, { { 0, 1 }, { 2, 1 }, { 4, 1 }, { 2, 1 }, { 0, 2 } })) });
            add ({ "the mystic chord", "mystic chord", P::western, "twentieth century", "Alexander Scriabin (1872–1915)", "1910", 1872, "chordal",
                   "A six-note chord of fourths (augmented, diminished and perfect) that stands as a centre in its own right, from "
                   "which the melodies of Prometheus are drawn.",
                   "C–F♯–B♭–E–A–D, a subset of the acoustic scale",
                   "Scriabin, Prometheus: The Poem of Fire, Op. 60 (1910); Perle, 'Scriabin's Self-Analyses'", "", 50.0,
                   chords ({ "1", "2", "3", "4", "5", "6" }, planed ({ 0, 6, 10, 16, 21, 26 }, { { 0, 4 }, { 2, 4 } })) });
            add ({ "free atonality and twelve-tone harmony", "atonality", P::western, "twentieth century", "Arnold Schoenberg (1874–1951)", "1908–1923", 1874, "chordal",
                   "Harmony emancipated from tonal resolution: chords chosen by interval content (pitch-class sets), and in the "
                   "twelve-tone method drawn from the segments of a row.",
                   "set classes such as 3-5 and 4-19; the all-interval tetrachords; row segments as chords",
                   "Schoenberg, Harmonielehre (1911); Forte, The Structure of Atonal Music (1973)", "", 60.0,
                   chords ({ "1", "2", "3", "4" }, { { { 0, 6, 11, 17 }, 2 }, { { 1, 5, 10, 16 }, 2 }, { { -1, 4, 8, 15 }, 2 }, { { 0, 3, 9, 14 }, 2 } }) });
            add ({ "polytonality", "polytonality", P::western, "twentieth century", "Igor Stravinsky (1882–1971)", "1911", 1882, "chordal",
                   "Two keys at once: the Petrushka chord sets C major against F♯ major, a tritone apart; Milhaud and Ives took it "
                   "further. The octatonic scale often holds both.",
                   "two triads a tritone apart (C and F♯); octatonic collections",
                   "Stravinsky, Petrushka (1911); Milhaud, 'Polytonalité et atonalité' (1923); van den Toorn, The Music of Igor Stravinsky", "", 90.0,
                   chords ({ "C", "E", "G", "F♯", "A♯", "C♯" }, { { { 0, 4, 7, 18, 22, 25 }, 2 }, { { 4, 7, 12, 22, 25, 30 }, 2 }, { { 0, 4, 7, 18, 22, 25 }, 4 } }) });
            add ({ "quartal harmony", "quartal harmony", P::western, "twentieth century", "Paul Hindemith (1895–1963)", "1937", 1895, "chordal",
                   "Chords stacked in fourths instead of thirds, with no root by default: in Schoenberg's Chamber Symphony, Bartók, "
                   "Hindemith's theory of chord values, and from there jazz.",
                   "stacked perfect fourths; the fourth chord and its inversions",
                   "Schoenberg, Kammersymphonie Op. 9 (1906); Hindemith, Unterweisung im Tonsatz (1937); Persichetti, Twentieth-Century Harmony (1961)", "", 66.0,
                   chords ({ "1", "2", "3", "4" }, planed ({ 0, 5, 10, 15 }, { { 0, 2 }, { 2, 2 }, { 5, 2 }, { 0, 2 } })) });
            add ({ "modal harmony", "modal harmony", P::western, "twentieth century", "the modal revival, modal jazz and after", "c. 1890–", 1890, "chordal",
                   "Chords built from a mode rather than a major or minor key, so the mode's own colour stays: the ear is held on its "
                   "root by staying on the tonic chord or a pedal, the V–I cadence that would turn it back into major or minor is "
                   "avoided, chords move by step rather than by fifth, and the chords chosen carry the mode's characteristic note "
                   "(dorian's raised sixth, mixolydian's flat seventh, lydian's raised fourth, phrygian's flat second) without a "
                   "tritone that would make them sound dominant. Quartal voicings keep it open. From Debussy, Fauré and Vaughan "
                   "Williams's modal revival to Kind of Blue, My Favorite Things, Impressions and Footprints, and the ♭VII–I of rock "
                   "and the dorian vamps of film scores. (Mode ▸ jazz has its quartal side; Renaissance modal counterpoint is the older, "
                   "separate sense.) The example plays each mode on the same final over a pedal.",
                   "dorian i–IV, mixolydian I–♭VII, lydian I–II, phrygian i–♭II; pedals; stacked fourths",
                   "Pease, Jazz Composition: Theory and Practice (Berklee Press, 2003); Persichetti, Twentieth-Century Harmony (1961); "
                   "Miller, Modal Jazz Composition and Harmony (1996); Berklee Online, 'Modal Harmony in Jazz Composition'; My New "
                   "Microphone, Hub Guitar, Chromatone and Audiofanzine guides to modal harmony; Davis, So What (1959)", "", 76.0,
                   { line ("pedal", "0:16"), line ("lower", "3:2 5:2 4:2 2:2 4:2 2:2 3:2 1:2"), line ("middle", "7:2 9:2 7:2 5:2 7:2 6:2 7:2 5:2"),
                     line ("upper", "10:2 12:2 12:2 10:2 11:2 9:2 12:2 8:2") } });
            add ({ "pandiatonicism", "pandiatonicism", P::western, "twentieth century", "Aaron Copland (1900–1990)", "1930s–1940s", 1900, "chordal",
                   "Every note of a diatonic scale free to sound with every other, without functional progression: the open, added-note "
                   "sound of Copland's Appalachian Spring and Stravinsky's neoclassicism. Slonimsky's term.",
                   "triads with added seconds, fourths and sixths; diatonic clusters",
                   "Slonimsky, Music since 1900 (1937); Copland, Appalachian Spring (1944)", "", 66.0,
                   chords ({ "1", "2", "3", "4", "5" }, { { { -12, 7, 14, 16, 19 }, 2 }, { { -7, 5, 9, 12, 14 }, 2 }, { { -12, 2, 7, 11, 16 }, 2 }, { { -12, 4, 7, 9, 14 }, 2 } }) });
            add ({ "minimalist harmony", "minimalist harmony", P::western, "twentieth century", "Steve Reich (b. 1936)", "1960s–", 1936, "chordal",
                   "Slow harmonic rhythm over pulsing patterns: a chord, or a cycle of a few, held while the texture changes inside it, "
                   "as in Reich's Music for 18 Musicians and Glass's arpeggiated progressions.",
                   "diatonic chords with added notes, changing slowly; static drones",
                   "Reich, Music for 18 Musicians (1976); Glass, Einstein on the Beach (1976); Potter, Four Musical Minimalists", "", 120.0,
                   { line ("bass", "-3:4 -7:4 -5:4 -10:4"), line ("pulse", "4:0.5 4:0.5 4:0.5 4:0.5 4:0.5 4:0.5 4:0.5 4:0.5 "
                                                                           "5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 "
                                                                           "7:0.5 7:0.5 7:0.5 7:0.5 7:0.5 7:0.5 7:0.5 7:0.5 "
                                                                           "5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 5:0.5 5:0.5"),
                     line ("upper", "9:4 9:4 11:4 9:4"), line ("top", "14:4 12:4 14:4 14:4") } });
            add ({ "spectral harmony", "spectral harmony", P::western, "twentieth century", "Gérard Grisey (1946–1998)", "1975", 1946, "overtone",
                   "Chords made from the partials of a sound, its harmonic spectrum or an analysed one, heard as a single timbre; the "
                   "music of L'Itinéraire, Grisey's Partiels and Murail's Gondwana.",
                   "the harmonic series, its seventh and eleventh partials rounded here to the nearest key",
                   "Grisey, Partiels (1975); Murail, 'The Revolution of Complex Sounds' (2005); Fineberg, 'Guide to the Basic Concepts of Spectral Music'", "", 50.0,
                   { line ("partial 1", "-24:8"), line ("partial 2", "-:0.5 -12:7.5"), line ("partial 3", "-:1 -5:7"), line ("partial 4", "-:1.5 0:6.5"),
                     line ("partial 5", "-:2 4:6"), line ("partial 6", "-:2.5 7:5.5"), line ("partial 7", "-:3 10:5"), line ("partial 8", "-:3.5 12:4.5") } });

            // ---- jazz ----
            add ({ "twelve-bar blues", "twelve-bar blues", P::jazz, "", "the blues", "c. 1900–", 1900, "functional",
                   "Twelve bars on I, IV and V, each a dominant seventh, the blue third and seventh sung against them: the dominant "
                   "seventh as a tonic, the root of jazz harmony.",
                   "I7, IV7, V7; the blue notes ♭3, ♭5, ♭7 against major chords",
                   "Handy, St. Louis Blues (1914); Titon, Early Downhome Blues; Gioia, The History of Jazz", "", 100.0,
                   chords (satb, planed ({ -12, 4, 7, 10 }, { { 0, 4 }, { 5, 4 }, { 0, 4 }, { 0, 4 }, { 5, 4 }, { 5, 4 }, { 0, 4 }, { 0, 4 },
                                                         { 7, 4 }, { 5, 4 }, { 0, 4 }, { 7, 4 } })) });
            add ({ "extended tertian chords", "extended chords", P::jazz, "", "swing", "1930s", 1930, "chordal",
                   "Sevenths as the norm and ninths, elevenths and thirteenths added above: the sixth and ninth chords of swing "
                   "sections, voiced close in the saxophones and brass.",
                   "6, maj7, 9, 11, 13 chords; the four-way close voicing",
                   "Levine, The Jazz Theory Book (1995); Ellington, Mood Indigo (1930)", "", 110.0,
                   chords ({ "bass", "1", "2", "3", "4" }, { { { -12, 4, 7, 9, 14 }, 4 }, { { -3, 7, 10, 12, 16 }, 4 }, { { -10, 5, 9, 12, 16 }, 4 }, { { -5, 5, 9, 11, 16 }, 4 } }) });
            add ({ "ii–v–i and tritone substitution", "ii-v-i", P::jazz, "", "bebop", "1940s", 1940, "functional",
                   "The ii–V–I as the unit of a tune, and the dominant replaced by the one a tritone away, which shares its third and "
                   "seventh, giving the chromatic bass of bebop: Dm7–D♭7–Cmaj7.",
                   "m7–7–maj7; the tritone substitute ♭II7; altered dominants (♭9, ♯9, ♯11, ♭13)",
                   "Levine, The Jazz Theory Book; Parker & Gillespie, Groovin' High (1945); Owens, Bird: The Life and Music of Charlie Parker", "", 140.0,
                   chords ({ "bass", "1", "2", "3" }, { { { -10, 5, 9, 12 }, 4 }, { { -11, 5, 11, 15 }, 4 }, { { -12, 4, 11, 14 }, 8 } }) });
            add ({ "rootless voicings", "rootless voicings", P::jazz, "", "Bill Evans (1929–1980)", "1958–", 1958, "chordal",
                   "The left hand leaves the root to the bass and plays third, seventh and colour tones, in two shapes (A and B) that "
                   "move by step through a ii–V–I.",
                   "3–5–7–9 and 7–9–3–5; G13 as F–A–B–E",
                   "Levine, The Jazz Piano Book (1989); Evans, Portrait in Jazz (1959)", "", 120.0,
                   chords ({ "bass", "1", "2", "3", "4" }, { { { -10, 5, 9, 12, 16 }, 4 }, { { -5, 5, 9, 11, 16 }, 4 }, { { -12, 4, 7, 11, 14 }, 8 } }) });
            add ({ "modal and quartal jazz", "modal jazz", P::jazz, "", "Miles Davis and Bill Evans", "1959", 1959, "chordal",
                   "A mode held for many bars instead of changing chords, voiced in fourths: the 'So What' chord (three fourths and a "
                   "major third) and McCoy Tyner's fourths.",
                   "the So What chord; stacked fourths moved in parallel within the dorian mode",
                   "Davis, Kind of Blue (1959); Russell, The Lydian Chromatic Concept of Tonal Organization (1953)", "", 136.0,
                   chords ({ "bass", "1", "2", "3", "4", "5" }, { { { -12, 2, 7, 12, 17, 21 }, 3 }, { { -12, 0, 5, 10, 15, 19 }, 5 },
                                                                    { { -12, 2, 7, 12, 17, 21 }, 3 }, { { -12, 0, 5, 10, 15, 19 }, 5 } }) });
            add ({ "coltrane changes", "coltrane changes", P::jazz, "", "John Coltrane (1926–1967)", "1959", 1960, "functional",
                   "Three keys a major third apart, each reached by its own dominant, dividing the octave in three: Giant Steps' "
                   "B–D7–G–B♭7–E♭.",
                   "maj7 chords a major third apart, each with its V7",
                   "Coltrane, Giant Steps (1960); Demsey, John Coltrane Plays Giant Steps", "", 120.0,
                   chords ({ "bass", "1", "2", "3" }, { { { -13, 3, 6, 10 }, 2 }, { { -10, 2, 6, 12 }, 2 }, { { -5, 2, 7, 11 }, 2 },
                                                       { { -14, 2, 5, 8 }, 2 }, { { -9, 3, 7, 10 }, 4 } }) });
            add ({ "upper-structure triads", "upper structures", P::jazz, "", "post-bop", "1960s–", 1960, "chordal",
                   "A triad played above a dominant's tritone so that its notes are the tensions: D major over C7 gives 9, ♯11 and 13; "
                   "A♭ over C7 gives ♭13, root and ♯9.",
                   "a major triad over the 3rd and 7th of a dominant; the altered scale",
                   "Levine, The Jazz Piano Book; Mehegan, Jazz Improvisation (1959)", "", 90.0,
                   chords ({ "bass", "3rd", "7th", "1", "2", "3" }, { { { -12, 4, 10, 14, 18, 21 }, 4 }, { { -12, 4, 10, 15, 20, 24 }, 4 }, { { -7, 4, 9, 12, 16, 19 }, 8 } }) });
            add ({ "reharmonization and block chords", "block chords", P::jazz, "", "George Shearing (1919–2011)", "1949", 1949, "parallel",
                   "A melody harmonized note by note, the melody doubled an octave below with a chord in between ('locked hands'), "
                   "passing notes taken as diminished sevenths.",
                   "drop-2 and locked-hands voicings; the diminished-seventh passing chord",
                   "Shearing, September in the Rain (1949); Levine, The Jazz Piano Book", "", 120.0,
                   chords ({ "1", "2", "3", "4", "5" }, { { { 4, 7, 9, 12, 16 }, 1 }, { { 5, 8, 11, 14, 17 }, 1 }, { { 7, 12, 16, 19 - 2, 19 }, 1 }, { { 4, 7, 9, 12, 16 }, 1 } }) });

            // ---- the world, by region ----
            add ({ "georgian polyphony", "georgian polyphony", P::world, "europe", "Georgia", "first written 11th c.", 1000, "polyphony",
                   "Three-voice singing, in the east over a held bass drone and in the west (Guria, Svaneti) in three independent "
                   "parts, with sharp seconds, fourths and sevenths resolving to the fifth-and-octave, in a tuning of its own. UNESCO 2001.",
                   "fifth plus octave (or fourth) at cadences; seconds, fourths and sevenths in passing; a non-tempered scale",
                   "Jordania, Who Asked the First Question? (2006); Erkomaishvili archive; Scherbaum et al. on Georgian tuning", "", 66.0,
                   { line ("bani (bass)", "0:8"), line ("modzakhe", "7:2 5:1 7:1 5:2 7:2"), line ("mtkmeli", "10:2 7:1 9:1 10:2 12:2") } });
            add ({ "byzantine ison", "byzantine ison", P::world, "europe", "Byzantine and Greek Orthodox chant", "15th c.–", 1400, "drone",
                   "The chant sung over the ison, a held note on the base of the echos, moving with the melody's centre.",
                   "the melody against its drone: unisons, fourths and fifths at rest, every interval of the echos in passing",
                   "Levy & Troelsgård, 'Byzantine chant' (Grove); Conomos", "echos protos", 60.0,
                   { line ("ison", "0:8"), line ("melos", "0:1 2:1 3:1 5:2 3:1 2:1 0:1") } });
            add ({ "albanian iso-polyphony", "albanian iso-polyphony", P::world, "europe", "Albania (Tosk and Lab)", "", 1500, "drone",
                   "Two or three soloists, the 'taker' and the 'turner', in an anhemitonic pentatonic, over the iso, a drone sung by the "
                   "group on a vowel. UNESCO 2005.",
                   "seconds and fourths between soloists; the drone under both",
                   "Tole, Albanian Folk Iso-polyphony (2008); Ahmedaja, European Voices I", "", 70.0,
                   { line ("iso", "0:8"), line ("kthyes", "7:2 9:1 7:1 5:2 7:2"), line ("marrës", "10:2 12:1 10:1 9:2 7:2") } });
            add ({ "shope diaphony", "shope diaphony", P::world, "europe", "Bulgaria (Shopluk)", "", 1500, "drone",
                   "Two-part singing in which one voice holds a drone and the other moves close above and below it, prizing the "
                   "beating major second as the most beautiful sound.",
                   "the major second against the drone; unisons at the ends",
                   "Messner, Die Schwebungsdiaphonie in Bistrica (1980); Rice, May It Fill Your Soul", "", 70.0,
                   { line ("drone", "0:8"), line ("leader", "2:1 0:1 2:2 3:1 2:1 0:2") } });
            add ({ "cantu a tenore", "cantu a tenore", P::world, "europe", "Sardinia", "", 1500, "chordal",
                   "Four men: the boghe leads, the bassu and contra answer in guttural voices a fifth apart, the mesu boghe ornaments "
                   "above; the chord is built on the major triad. UNESCO 2005.",
                   "root, fifth and octave from the bassu, contra and mesu boghe; the boghe's melody above",
                   "Lortat-Jacob, Chants de Passion (1998); Macchiarella", "", 72.0,
                   { line ("bassu", "0:8"), line ("contra", "7:8"), line ("mesu boghe", "12:4 14:2 12:2"), line ("boghe", "16:1 17:1 19:2 17:1 16:1 14:1 12:1") } });
            add ({ "paghjella", "paghjella", P::world, "europe", "Corsica", "", 1500, "polyphony",
                   "Three men's voices: the seconda gives the melody, the bassu supports it, the terza decorates high above with "
                   "melismas; sung in a close huddle. UNESCO 2009.",
                   "fifths and octaves at cadences, thirds and seconds between",
                   "Bithell, Transported by Song: Corsican Voices (2007)", "", 60.0,
                   { line ("bassu", "0:2 -2:2 0:4"), line ("seconda", "7:2 5:2 7:4"), line ("terza", "10:1 12:1 9:1 10:1 12:4") } });
            add ({ "sutartinės", "sutartines", P::world, "europe", "Lithuania", "", 1500, "polyphony",
                   "Women's multipart songs in which voices meet in parallel and crossing seconds, the seconds heard as consonant; "
                   "two-part, three-part canons, and four-part as two pairs. UNESCO 2010.",
                   "major seconds as the stable interval",
                   "Račiūnaitė-Vyčinienė, Sutartinės: Lithuanian Polyphonic Songs (2002)", "", 80.0,
                   { line ("first", "4:1 2:1 4:1 2:1 4:1 2:1 4:1 2:1"), line ("second", "2:1 4:1 2:1 4:1 2:1 4:1 2:1 4:1") } });
            add ({ "seto leelo", "seto leelo", P::world, "europe", "Seto (Estonia)", "", 1500, "polyphony",
                   "A lead singer gives each line and the choir takes it up in two parts: the low torrõ, many voices, and the single "
                   "high killõ above, meeting in seconds, fourths and fifths. UNESCO 2009.",
                   "the killõ a second, fourth or fifth above the torrõ; unison endings",
                   "Pärtlas, 'Seto Polyphonic Song' (2006)", "", 72.0,
                   { line ("lead", "0:1 2:1 4:1 2:1 -:4"), line ("torrõ", "-:4 0:1 2:1 4:1 0:1"), line ("killõ", "-:4 4:1 7:1 7:1 2:1") } });
            add ({ "russian village polyphony", "russian village polyphony", P::world, "europe", "Russia and Ukraine", "", 1500, "heterophony",
                   "Protyazhnaya (drawn-out) songs where the voices branch from the tune as podgoloski, 'under-voices', each a variant "
                   "of it, converging on the unison or octave at the ends of phrases.",
                   "unison and octave at cadences; thirds, fourths and fifths where the variants part",
                   "Lineva, Great Russian Songs in Folk Harmonization (1904); Mazo; Ivanytsky on Ukrainian polyphony", "", 56.0,
                   { line ("tune", "0:1 2:1 3:2 5:1 3:1 2:1 0:1"), line ("podgolosok low", "0:1 -2:1 -5:2 -2:1 0:1 -2:1 0:1"),
                     line ("podgolosok high", "7:1 7:1 10:1 8:1 8:1 7:1 5:1 7:1") } });
            add ({ "naturjodel", "naturjodel", P::world, "europe", "Alpine (Switzerland and Austria)", "", 1700, "chordal",
                   "Wordless yodels flipping between chest and head voice, sung in two to four parts in thirds and sixths over a "
                   "bass on tonic and dominant, sometimes with the natural, sharpened fourth of the alphorn.",
                   "thirds and sixths over I and V; the alphorn fa (an 11th partial)",
                   "Baumann, Das sogenannte Alphorn-Fa (1976); Wise, Yodel in Switzerland", "", 66.0,
                   { line ("bass", "-12:2 -5:2 -5:2 -12:2"), line ("second", "4:1 7:1 2:1 5:1 7:1 5:1 4:2"), line ("yodel", "7:1 12:1 11:1 14:1 11:1 7:1 12:2") } });
            add ({ "klapa", "klapa", P::world, "europe", "Dalmatia (Croatia)", "19th c.–", 1850, "chordal",
                   "Small groups of men (now women too) singing a cappella in close tertian harmony: first tenor leads, second tenor "
                   "and baritone fill in, the bass holds the root, the chords lingering and swelling. UNESCO 2012.",
                   "close triads and dominant sevenths; the lead's melody over a sustained bass",
                   "Ćaleta, 'Klapa singing' (1997)", "", 60.0,
                   chords (satb, { { { -12, 4, 7, 12 }, 2 }, { { -7, 5, 9, 12 }, 2 }, { { -5, 5, 7, 11 }, 2 }, { { -12, 4, 7, 12 }, 2 } }) });

            add ({ "takht heterophony", "takht heterophony", P::world, "west asia", "Egypt and the Levant", "", 1800, "heterophony",
                   "The oud, qānūn, nāy and violin of a takht play one melody in a maqām each in their own way, at once, with the "
                   "singer: harmony as many versions of one line.",
                   "the unison and octave, and the passing seconds where the versions differ",
                   "Racy, Making Music in the Arab World (2003); Marcus, Music in Egypt", "rast", 80.0,
                   { line ("voice", "0:1 2:1 4:1 5:1 7:2 5:1 4:1"), line ("oud", "0:0.5 0:0.5 2:1 4:0.5 2:0.5 5:1 7:1 7:1 5:1 4:1"),
                     line ("qanun", "12:0.5 14:0.5 14:1 16:1 17:0.5 16:0.5 19:2 17:1 16:1"), line ("nay", "12:1 14:0.5 12:0.5 16:1 17:1 19:1.5 17:0.5 17:1 16:1") } });

            add ({ "dombra küi", "dombra kui", P::world, "central asia", "Kazakhs", "", 1700, "parallel",
                   "The two-stringed dombra tuned a fourth or fifth apart, the lower string a drone or moving in parallel with the "
                   "upper: the harmony of the instrumental küi.",
                   "fourths and fifths between the strings",
                   "Utegalieva; Levin, The Hundred Thousand Fools of God", "", 120.0,
                   { line ("lower string", "0:1 0:1 2:1 0:1 0:1 0:1 -2:1 0:1"), line ("upper string", "5:1 7:1 7:1 9:1 7:1 5:1 3:1 5:1") } });
            add ({ "xöömei", "xoomei", P::world, "central asia", "Tuva and Mongolia", "", 1700, "overtone",
                   "One singer, two parts: the fundamental held as a drone while the throat and mouth pick out its overtones, the 6th "
                   "to the 12th, as a melody. Mongolian khöömii, UNESCO 2010.",
                   "a drone and its harmonics: octave, fifth, major third and the flat seventh of nature",
                   "Levin & Süzükei, Where Rivers and Mountains Sing (2006)", "", 60.0,
                   { line ("drone", "-12:8"), line ("overtones", "24:1 26:1 28:2 26:1 24:1 19:2") } });
            add ({ "urtyn duu with the morin khuur", "urtyn duu", P::world, "central asia", "Mongolia", "", 1700, "heterophony",
                   "The long song, free in time, and the horse-head fiddle that follows it a little behind or ahead, ornamenting the same "
                   "line. UNESCO 2005.",
                   "unisons and octaves, seconds in passing",
                   "Pegg, Mongolian Music, Dance, and Oral Narrative (2001)", "", 50.0,
                   { line ("voice", "7:2 9:1 12:3 9:1 7:1"), line ("morin khuur", "7:1 9:1 9:1 12:1 12:2 9:1 7:1") } });

            add ({ "tānpurā drone", "tanpura drone", P::world, "south asia", "Hindustani and Karnatak", "", 1500, "drone",
                   "The tānpurā's sa and pa sounding all the time, the melody heard against it: each svara a consonance or a tension "
                   "with the drone and its overtones, and the rāga's character in how it sits there.",
                   "the drone's sa, pa (or ma, or ni) and their harmonics against each svara",
                   "Jairazbhoy, The Rāgs of North Indian Music (1971); Raman on the tanpura's jawari", "", 60.0,
                   { line ("tanpura", "-5:1 0:1 0:1 -12:1 -5:1 0:1 0:1 -12:1"), line ("melody", "12:1 14:1 16:2 18:1 19:2 16:1") } });

            add ({ "gamelan layering", "gamelan layering", P::world, "southeast asia", "Java", "", 1600, "stratified",
                   "Stratified polyphony: a core melody (balungan) on the sarons, the same line elaborated faster and higher by the "
                   "peking and bonangs, and punctuated by kenong and gong at the ends of its phrases (colotomy); octaves and the "
                   "kempyung (a gap of two keys) sound together.",
                   "octaves (gembyang) and kempyung; the gong at the end of the phrase",
                   "Sumarsam, Gamelan (1995); Becker, Traditional Music in Modern Java; Hood, The Nuclear Theme as a Determinant of Patet", "slendro", 80.0,
                   { line ("saron", "5:1 7:1 10:1 7:1 2:1 5:1 7:1 10:1"),
                     line ("peking", "17:0.5 17:0.5 19:0.5 19:0.5 22:0.5 22:0.5 19:0.5 19:0.5 14:0.5 14:0.5 17:0.5 17:0.5 19:0.5 19:0.5 22:0.5 22:0.5"),
                     line ("kenong", "-:3 -5:1 -:3 -2:1"), line ("gong", "-:7 -14:1") } });
            add ({ "kotekan", "kotekan", P::world, "southeast asia", "Bali", "gong kebyar, 1910s", 1910, "interlocking",
                   "Two parts, polos and sangsih, each playing only some of the notes of a fast figuration that only sounds whole "
                   "together; the paired instruments tuned apart so their unisons shimmer (ombak).",
                   "the interlocked line; seconds and thirds where the parts overlap",
                   "Tenzer, Gamelan Gong Kebyar (2000); McPhee, Music in Bali (1966)", "", 80.0,
                   { line ("pokok", "-12:1 -9:1 -5:1 -4:1"),
                     line ("polos", "0:0.25 -:0.25 3:0.25 -:0.25 0:0.25 -:0.25 7:0.25 -:0.25 8:0.25 -:0.25 3:0.25 -:0.25 7:0.25 -:0.25 8:0.25 -:0.25"),
                     line ("sangsih", "-:0.25 1:0.25 -:0.25 1:0.25 -:0.25 3:0.25 -:0.25 8:0.25 -:0.25 7:0.25 -:0.25 1:0.25 -:0.25 8:0.25 -:0.25 7:0.25") } });
            add ({ "khaen", "khaen", P::world, "southeast asia", "Lao", "", 1600, "drone",
                   "The free-reed mouth organ of the Lao, playing melody over its own held drones and in parallel octaves, fourths "
                   "and fifths, in the lai modes. UNESCO 2017.",
                   "drones on the tonic and fifth; melody doubled a fourth below",
                   "Miller, Traditional Music of the Lao (1985)", "", 90.0,
                   { line ("drone", "0:8"), line ("drone fifth", "7:8"), line ("lower", "9:1 7:1 5:1 7:1 9:2 7:2"), line ("melody", "14:1 12:1 10:1 12:1 14:2 12:2") } });
            add ({ "shō clusters (aitake)", "aitake", P::world, "east asia", "Japanese court (gagaku)", "8th c.–", 700, "cluster",
                   "The mouth organ of gagaku holds a cluster of five or six pipes, an aitake named for its lowest note, and slides "
                   "between them; the hichiriki and ryūteki play the melody heterophonically through it. (The cluster here is in the "
                   "manner of an aitake, not a particular one.)",
                   "clusters of seconds, fourths and fifths",
                   "Momii, 'A Transformational Approach to Gesture in Shō Performance', Music Theory Online 26.4 (2020); Garfias, Music of a Thousand Autumns", "", 40.0,
                   { line ("shō 1", "2:4 0:4"), line ("shō 2", "7:4 7:4"), line ("shō 3", "9:4 9:4"), line ("shō 4", "12:4 12:4"), line ("shō 5", "14:4 16:4"),
                     line ("hichiriki", "14:2 16:2 19:2 16:2") } });
            add ({ "grand song", "grand song", P::world, "east asia", "Dong (Guizhou)", "", 1000, "drone",
                   "Kgal laox, sung without conductor or instruments: many voices hold and sway on a low part while one or two "
                   "soloists sing the high part, imitating birds and cicadas. UNESCO 2009.",
                   "seconds, thirds and fourths over the low held part",
                   "Ingram, Singing the Dong Grand Song (2012)", "", 60.0,
                   { line ("low", "0:4 -2:2 0:2"), line ("high", "7:1 9:1 7:1 5:1 7:2 3:1 2:1") } });
            add ({ "sizhu heterophony", "sizhu heterophony", P::world, "east asia", "Jiangnan (Shanghai)", "", 1800, "heterophony",
                   "Silk-and-bamboo ensembles: erhu, dizi, pipa, yangqin and xiao each realize the same skeletal tune, like Lao liuban, "
                   "with their own density and ornament.",
                   "unisons, octaves and passing seconds between versions",
                   "Witzleben, 'Silk and Bamboo' Music in Shanghai (1995)", "", 76.0,
                   { line ("erhu", "12:1 14:1 16:1 19:1 21:1 19:1 16:1 14:1"),
                     line ("dizi", "24:0.5 26:0.5 26:0.5 28:0.5 28:0.5 31:0.5 31:1 33:0.5 31:0.5 31:0.5 28:0.5 28:1 26:1"),
                     line ("pipa", "0:0.5 0:0.5 2:0.5 2:0.5 4:0.5 4:0.5 7:0.5 7:0.5 9:0.5 9:0.5 7:0.5 7:0.5 4:0.5 4:0.5 2:0.5 2:0.5") } });
            add ({ "sinawi", "sinawi", P::world, "east asia", "Korea (Jeolla)", "", 1800, "heterophony",
                   "From shaman ritual music: piri, daegeum, haegeum and ajaeng improvise at once in one mode, close and apart, over a "
                   "janggu rhythm: a deliberate 'disorder' that is the point.",
                   "unisons and octaves pulled apart into seconds and fourths",
                   "Howard, Korean Music: A Listening Guide; Lee Byong-won", "", 70.0,
                   { line ("ajaeng", "0:4 -2:2 0:2"), line ("piri", "12:1 15:1 17:2 15:1 12:1 10:2"), line ("daegeum", "24:2 22:1 24:1 27:2 24:2") } });
            add ({ "upopo rounds", "upopo", P::world, "east asia", "Ainu (Hokkaido)", "", 1500, "polyphony",
                   "Women seated round a lacquered chest sing upopo as ukouk: the same short phrase in canon, entering one after "
                   "another, so the round makes the harmony. UNESCO 2009 (Ainu traditional dance).",
                   "the canon's overlaps: seconds, fourths and fifths",
                   "Tanimoto Kazuyuki; Chiba, Ainu Music", "", 90.0,
                   { line ("first", "7:1 5:1 7:1 2:1 7:1 5:1 7:1 2:1"), line ("second", "-:1 7:1 5:1 7:1 2:1 7:1 5:1 7:1"),
                     line ("third", "-:2 7:1 5:1 7:1 2:1 7:1 5:1") } });
            add ({ "pasibutbut", "pasibutbut", P::world, "east asia", "Bunun (Taiwan)", "", 1800, "chordal",
                   "The prayer for millet: men in a circle sustain a chord of octave, fifth and third and raise it slowly, in tiny "
                   "steps, the higher it reaches the better the harvest.",
                   "root, fifth, octave and major third, rising",
                   "Kurosawa (1943); Wu Rung-shun", "", 40.0,
                   chords ({ "bass", "fifth", "octave", "third" }, planed ({ 0, 7, 12, 16 }, { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 } })) });

            add ({ "aka and baka polyphony", "aka polyphony", P::world, "africa", "Aka and Baka (Central Africa)", "", 1000, "polyphony",
                   "Contrapuntal singing in four parts on short ostinatos, each varied and yodelled, the main tune (mòtángòlè) and "
                   "its response (ósêsê) and two more, overlapping into a dense pentatonic weave. UNESCO 2003.",
                   "pentatonic unisons, fourths, fifths and octaves in constantly changing combinations",
                   "Arom, African Polyphony and Polyrhythm (1991); Fürniss, 'What is a part?' (2016)", "", 100.0,
                   { line ("mòtángòlè", "7:1 9:1 7:1 4:1 2:1 4:1 7:2"), line ("ósêsê", "0:2 -3:1 0:1 2:2 0:2"),
                     line ("third part", "12:1 -:1 14:1 12:1 9:1 -:1 12:2"), line ("yodel", "19:0.5 16:0.5 -:1 21:0.5 16:0.5 -:1 19:0.5 14:0.5 -:1 16:1 -:1") } });
            add ({ "horn hocket", "horn hocket", P::world, "africa", "Banda-Linda (Central African Republic)", "", 1000, "interlocking",
                   "An orchestra of horns, each playing one or two pitches at its own moment, so that melody and chords come only from "
                   "their sum.",
                   "the composite line; fourths and octaves where horns coincide",
                   "Arom, African Polyphony and Polyrhythm (1991)", "", 120.0,
                   { line ("horn 1", "0:1 -:1 0:1 -:1"), line ("horn 2", "-:0.5 4:0.5 -:1 -:0.5 4:0.5 -:1"), line ("horn 3", "-:1 7:1 -:1 7:0.5 -:0.5"),
                     line ("horn 4", "-:1.5 9:0.5 -:1.5 12:0.5"), line ("horn 5", "-:0.5 -:0.5 -:0.5 14:0.5 -:2") } });
            add ({ "akan thirds", "akan thirds", P::world, "africa", "Akan (Ghana)", "", 1500, "parallel",
                   "Call and response with the chorus in parallel thirds; Nketia mapped Africa's harmony in zones, here the thirds, "
                   "elsewhere fourths, fifths and octaves.",
                   "parallel thirds, at cadences the unison or octave",
                   "Nketia, The Music of Africa (1974)", "", 100.0,
                   { line ("call", "16:1 14:1 12:1 11:1 12:4"), line ("chorus low", "-:4 9:1 7:1 5:1 7:1"), line ("chorus high", "-:4 12:1 11:1 9:1 11:1") } });
            add ({ "mbira interlocking", "mbira interlocking", P::world, "africa", "Shona (Zimbabwe)", "", 1500, "interlocking",
                   "Two mbira dzavadzimu: kushaura leads and kutsinhira interlocks, each part itself in two hands, in a cycle of four "
                   "phrases of twelve pulses whose dyads give a harmonic progression.",
                   "dyads of fourths, fifths and thirds, cycling",
                   "Berliner, The Soul of Mbira (1978); Tracey, 'The system of the mbira' (1989)", "", 120.0,
                   { line ("kushaura", "0:0.5 -:0.5 7:0.5 -:0.5 0:0.5 -:0.5 5:0.5 -:0.5 9:0.5 -:0.5 5:0.5 -:0.5"),
                     line ("kushaura high", "12:0.5 -:0.5 16:0.5 -:0.5 12:0.5 -:0.5 12:0.5 -:0.5 16:0.5 -:0.5 12:0.5 -:0.5"),
                     line ("kutsinhira", "-:0.5 4:0.5 -:0.5 7:0.5 -:0.5 5:0.5 -:0.5 9:0.5 -:0.5 7:0.5 -:0.5 4:0.5") } });
            add ({ "dorze polyphony", "dorze polyphony", P::world, "africa", "Dorze (Ethiopia)", "", 1500, "polyphony",
                   "Multipart singing in up to six parts, each with its name and role, on a pentatonic base, overlapping and hocketing, "
                   "unlike the monophony around it in Ethiopia.",
                   "pentatonic fourths, fifths and seconds between interwoven parts",
                   "Lortat-Jacob, 'Polyphonies dorzé' (Ethiopia: Polyphonies et techniques vocales, 1994)", "", 100.0,
                   { line ("1", "0:1 -:1 3:1 0:1 -:1 5:1 3:1 -:1"), line ("2", "-:1 7:1 5:1 -:1 7:1 10:1 -:1 7:1"), line ("3", "12:2 10:1 12:1 15:2 12:2") } });
            add ({ "two-root harmony", "two-root harmony", P::world, "africa", "Xhosa (South Africa)", "", 1500, "overtone",
                   "From the musical bows, uhadi and umrhubhe: two fundamentals a whole tone apart, each with its own overtones, "
                   "alternate as two 'chords'; the songs, and split-tone umngqokolo singing, follow them.",
                   "two roots a whole tone apart, each with its octave, fifth and major third",
                   "Dargie, Xhosa Music: Its Techniques and Instruments (1988)", "", 80.0,
                   chords ({ "root", "octave", "fifth", "2nd octave", "third" }, planed ({ -12, 0, 7, 12, 16 }, { { 0, 2 }, { 2, 2 }, { 0, 2 }, { 2, 2 } })) });

            add ({ "sikuri", "sikuri", P::world, "the americas", "Aymara (Andes)", "", 1500, "interlocking",
                   "Panpipes in halves, ira and arka, that alternate notes to make one melody; played in bands with pipes in octaves "
                   "and fifths, giving parallel harmony.",
                   "the hocket of the halves; parallel octaves and fifths between sizes",
                   "Turino, Moving Away from Silence (1993)", "", 110.0,
                   { line ("ira", "12:1 -:1 7:1 -:1 7:1 -:1 4:1 -:1"), line ("arka", "-:1 9:1 -:1 4:1 -:1 9:1 -:1 0:1"),
                     line ("ira (fifth below)", "5:1 -:1 0:1 -:1 0:1 -:1 -3:1 -:1"), line ("arka (fifth below)", "-:1 2:1 -:1 -3:1 -:1 2:1 -:1 -7:1") } });

            add ({ "'are'are panpipes", "are are panpipes", P::world, "oceania", "'Are'are (Solomon Islands)", "", 1500, "polyphony",
                   "Panpipe ensembles, and solo pipes built so two adjacent tubes sound together, in a nearly equal heptatonic tuning: "
                   "two or three kinds of dyad within each piece.",
                   "dyads of neighbouring pipes in seven near-equal steps",
                   "Zemp, ''Are'are Classification of Musical Types and Instruments' (1978); Rahn (2015, 2023)", "7 equal steps", 100.0,
                   { line ("upper", "7:1 9:1 12:1 9:1 7:1 5:1 7:2"), line ("lower", "5:1 7:1 9:1 7:1 5:1 3:1 5:2") } });
            add ({ "yiḏaki and manikay", "yidaki", P::world, "oceania", "Yolŋu (Arnhem Land)", "", 1000, "drone",
                   "Song (manikay) over the yiḏaki's drone and its overblown 'toot', with clapsticks: the voice falls through its "
                   "range against the unchanging fundamental.",
                   "the drone and its overblown upper note against a descending line",
                   "Knopoff, 'Yolŋu clan songs'; Toner", "", 100.0,
                   { line ("yiḏaki", "0:8"), line ("toot", "-:3 16:0.5 -:3 16:0.5 -:1"), line ("manikay", "19:1 17:1 16:2 14:1 12:1 12:2") } });

            // ---- traditions grown from meeting Western harmony ----
            add ({ "sacred harp (four shapes)", "sacred harp", P::meeting, "the americas", "Appalachian shape-note singing", "1844", 1844, "polyphony",
                   "Fasola singing from The Sacred Harp: four parts in a hollow square, the tune in the tenor, the treble and tenor "
                   "doubled by men and women, open fifths and octaves, parallel fifths unashamed, chords often without thirds.",
                   "open fifths and octaves; final chords without a third; dispersed harmony",
                   "White & King, The Sacred Harp (1844); Cobb, The Sacred Harp: A Tradition and Its Music; Miller, Traveling Home", "", 90.0,
                   { line ("bass", "-12:2 -7:2 -5:2 -12:2"), line ("tenor", "0:1 4:1 5:2 2:2 0:2"), line ("alto", "7:2 9:2 7:2 7:2"), line ("treble", "12:2 12:2 11:2 12:2") } });
            add ({ "southern harmony and christian harmony", "christian harmony", P::meeting, "the americas", "Appalachian shape-note singing", "1835–1866", 1835, "polyphony",
                   "William Walker's tunebooks: The Southern Harmony (1835) in four shapes and three parts, and The Christian Harmony "
                   "(1866) in seven shapes, still sung in the North Carolina and Alabama mountains; the same dispersed, fifth-rich harmony.",
                   "three parts in open fifths; the seven-shape do-re-mi",
                   "Walker, The Southern Harmony (1835), The Christian Harmony (1866); Jackson, White Spirituals in the Southern Uplands (1933)", "", 84.0,
                   { line ("bass", "-12:2 -5:2 -10:2 -12:2"), line ("tenor", "7:1 4:1 2:2 5:1 2:1 0:2"), line ("treble", "12:2 14:2 9:2 7:2") } });
            add ({ "seven-shape gospel", "seven-shape gospel", P::meeting, "the americas", "Appalachian shape-note singing", "1870s–", 1870, "functional",
                   "The seven-shape music of the Ruebush–Kieffer and James D. Vaughan publishers and the singing schools: functional "
                   "gospel harmony with a moving bass, the root of Southern gospel quartets.",
                   "I, IV, V7 and secondary dominants; the bass answering the upper voices",
                   "Aikin, The Christian Minstrel (1846); Harrison, 'Shape-note singing' (Grove); Goff, Close Harmony (2002)", "", 100.0,
                   chords (satb, { { { -12, 4, 7, 12 }, 2 }, { { -8, 4, 9, 13 }, 2 }, { { -10, 5, 9, 14 }, 2 }, { { -5, 5, 11, 14 }, 2 }, { { -12, 4, 7, 12 }, 4 } }) });
            add ({ "lining out (dr. watts hymnody)", "lining out", P::meeting, "the americas", "Black American choral harmony", "18th c.–", 1750, "heterophony",
                   "A leader lines out each line of an Isaac Watts hymn, and the congregation sings it back slowly, everyone "
                   "ornamenting the tune at once: a heterophony thick enough to be heard as harmony.",
                   "unisons and octaves spread into passing seconds, thirds and fourths",
                   "Dargan, Lining Out the Word (2006); Southern, The Music of Black Americans", "", 40.0,
                   { line ("leader", "0:1 4:1 7:2 -:4"), line ("congregation low", "-:4 0:1 2:1 4:1 0:1"), line ("congregation high", "-:4 12:1.5 14:0.5 16:1 12:1"),
                     line ("congregation middle", "-:4 7:1 9:1 7:1 7:1") } });
            add ({ "concert spiritual", "concert spiritual", P::meeting, "the americas", "Black American choral harmony", "1871", 1871, "functional",
                   "The Fisk Jubilee Singers took the spirituals into four-part concert arrangements; Burleigh, Dett and Hall Johnson "
                   "made them art song and choral repertory, keeping the pentatonic tunes and the call and response.",
                   "four-part hymn harmony on pentatonic tunes; the plagal cadence",
                   "Marsh, The Story of the Jubilee Singers (1875); Burleigh, Deep River (1916); Graham, Spirituals and the Birth of a Black Entertainment Industry", "", 60.0,
                   chords (satb, { { { -12, 4, 7, 12 }, 2 }, { { -9, 4, 9, 12 }, 2 }, { { -7, 5, 9, 12 }, 2 }, { { -12, 4, 7, 12 }, 2 } }) });
            add ({ "jubilee and gospel quartet", "gospel quartet", P::meeting, "the americas", "Black American choral harmony", "1930s", 1930, "chordal",
                   "Four or five men, the lead in the middle, the bass a rhythm instrument of its own: the jubilee style of the Golden "
                   "Gate Quartet and the hard gospel of the Soul Stirrers, ancestor of doo-wop and soul.",
                   "close dominant sevenths and sixths; the bass's walking lines",
                   "Seroff; Lornell, Happy in the Service of the Lord (1988)", "", 120.0,
                   { line ("bass", "-12:1 -8:1 -5:1 -8:1 -7:1 -3:1 0:1 -3:1"), line ("baritone", "4:2 4:2 5:2 5:2"),
                     line ("lead", "7:2 7:2 9:2 9:2"), line ("tenor", "10:2 12:2 12:2 14:2") } });
            add ({ "gospel choir harmony", "gospel choir", P::meeting, "the americas", "Black American choral harmony", "1930s–", 1932, "functional",
                   "Thomas A. Dorsey's gospel songs and the choirs that came after: three-part soprano, alto and tenor over piano and "
                   "organ, passing diminished chords, the IV–I 'amen' and the walk-ups of the bass.",
                   "triads in close position in three parts; the ♯iv°7 passing chord; the plagal cadence",
                   "Boyer, How Sweet the Sound (1995); Harris, The Rise of Gospel Blues (1992)", "", 72.0,
                   chords ({ "bass", "tenor", "alto", "soprano" }, { { { -12, 7, 12, 16 }, 2 }, { { -7, 9, 12, 17 }, 2 }, { { -6, 9, 12, 18 }, 2 },
                                                                    { { -5, 7, 12, 16 }, 2 }, { { -7, 9, 12, 17 }, 2 }, { { -12, 7, 12, 16 }, 2 } }) });
            add ({ "barbershop", "barbershop", P::meeting, "the americas", "United States", "1890s–", 1890, "chordal",
                   "Four voices in close harmony, the lead below the tenor, moving round the circle of fifths on dominant sevenths that "
                   "'ring' when tuned just, so their overtones lock.",
                   "the barbershop seventh; the circle of fifths; just-tuned major triads",
                   "Abbott, 'Play That! Barber Shop Harmony' (1992); Averill, Four Parts, No Waiting (2003)", "", 70.0,
                   chords ({ "bass", "baritone", "lead", "tenor" }, { { { -8, 2, 8, 11 }, 2 }, { { -3, 4, 7, 13 }, 2 }, { { -10, 6, 9, 12 }, 2 },
                                                                     { { -5, 5, 11, 14 }, 2 }, { { -12, 7, 12, 16 }, 2 } }) });
            add ({ "bossa nova", "bossa nova", P::meeting, "the americas", "Brazil", "1958", 1958, "chordal",
                   "Samba slowed and quieted, with jazz's sevenths, ninths and chromatic voice leading in the guitar's inner voices: "
                   "Jobim and João Gilberto.",
                   "maj7, m7, 7 with 9 and 13; II7 and ♭II7",
                   "Jobim, Garota de Ipanema (1962); Castro, Bossa Nova (2000)", "", 120.0,
                   chords ({ "bass", "1", "2", "3" }, { { { -12, 4, 7, 11 }, 4 }, { { -10, 6, 9, 12 }, 4 }, { { -10, 5, 9, 12 }, 2 }, { { -11, 5, 8, 11 }, 2 }, { { -12, 4, 7, 11 }, 4 } }) });
            add ({ "makwaya and isicathamiya", "isicathamiya", P::meeting, "africa", "Zulu and South African choirs", "1939", 1939, "chordal",
                   "Mission hymns remade: makwaya choirs, and isicathamiya's men's a cappella, with a call from the lead, a heavy "
                   "bass and the chorus answering on I, IV and I6/4–V: Solomon Linda's Mbube, Ladysmith Black Mambazo.",
                   "I–IV–I6/4–V; the deep bass and close upper parts",
                   "Erlmann, Nightsong (1996); Linda, Mbube (1939)", "", 80.0,
                   { line ("bass", "-12:2 -7:2 -5:2 -5:2"), line ("tenor", "4:2 5:2 0:1 2:1 2:2"), line ("alto", "7:2 9:2 7:2 7:2"),
                     line ("lead", "12:1 16:1 12:2 16:1 14:1 11:2") } });
            add ({ "bulgarian women's choir", "bulgarian choir", P::meeting, "europe", "Bulgaria", "1951", 1951, "chordal",
                   "Filip Kutev's state ensemble set village songs for women's choir, keeping the open-throated sound and the drone "
                   "and seconds of diaphony inside composed, often chromatic, harmony: Le Mystère des Voix Bulgares.",
                   "drones with clustered seconds; parallel and crossing parts; composed chromatic chords",
                   "Buchanan, Performing Democracy (2006); Rice, May It Fill Your Soul (1994)", "", 60.0,
                   { line ("alto 2", "0:8"), line ("alto 1", "0:2 2:2 3:2 2:2"), line ("soprano 2", "5:2 3:1 5:1 7:2 5:2"), line ("soprano 1", "7:2 7:1 8:1 10:2 7:2") } });
            add ({ "duets in thirds", "duets in thirds", P::meeting, "the americas", "Mexico and the Andes", "19th c.–", 1850, "parallel",
                   "Two voices a third (or sixth) apart through the whole song, over guitar or harp chords on I, IV and V: the "
                   "canción ranchera and huapango duos, the Andean huayno, and much of Latin American folk song from Spanish roots.",
                   "parallel thirds and sixths; I–IV–V",
                   "Sheehy, Mariachi Music in America (2006); Turino", "", 100.0,
                   { line ("bass", "-12:2 -7:2 -5:2 -12:2"), line ("second", "4:1 5:1 5:1 4:1 2:1 5:1 4:2"), line ("first", "7:1 9:1 9:1 7:1 5:1 7:1 7:2") } });
            add ({ "highlife", "highlife", P::meeting, "africa", "Ghana", "1920s–", 1920, "functional",
                   "Dance-band and guitar music that took I, IV and V from brass bands and hymns and set them in cycles over a timeline, "
                   "the guitar's thirds and sixths in two interlocking parts.",
                   "I–IV–I–V cycles; parallel thirds and sixths",
                   "Collins, Highlife Time (1996); Nketia", "", 120.0,
                   { line ("bass", "-12:2 -7:2 -12:2 -5:2"), line ("guitar low", "4:0.5 7:0.5 4:0.5 -:0.5 5:0.5 9:0.5 5:0.5 -:0.5 4:0.5 7:0.5 4:0.5 -:0.5 2:0.5 7:0.5 2:0.5 -:0.5"),
                     line ("guitar high", "-:0.5 12:0.5 -:0.5 16:0.5 -:0.5 12:0.5 -:0.5 17:0.5 -:0.5 12:0.5 -:0.5 16:0.5 -:0.5 11:0.5 -:0.5 14:0.5") } });
            add ({ "congolese rumba", "congolese rumba", P::meeting, "africa", "Congo", "1940s–", 1945, "interlocking",
                   "Cuban son records reheard in Léopoldville and Brazzaville, then guitars in interlocking parts on short I–IV–V "
                   "cycles, the voices in thirds: Franco, Tabu Ley, soukous. UNESCO 2021.",
                   "I–IV–V cycles; parallel thirds in the voices and guitars",
                   "Stewart, Rumba on the River (2000); White, Rumba Rules (2008)", "", 110.0,
                   { line ("bass", "-12:2 -7:2 -5:2 -7:2"), line ("guitar", "16:0.5 12:0.5 7:0.5 12:0.5 17:0.5 12:0.5 9:0.5 12:0.5 14:0.5 11:0.5 7:0.5 11:0.5 17:0.5 12:0.5 9:0.5 12:0.5"),
                     line ("voice low", "4:2 5:2 2:2 5:2"), line ("voice high", "7:2 9:2 5:2 9:2") } });
            add ({ "hīmene tārava", "himene tarava", P::meeting, "oceania", "Tahiti", "19th c.–", 1820, "polyphony",
                   "Hymn singing taken from the missionaries and made Polynesian: large choirs in many parts, a drone, a high descant, "
                   "and the men's rhythmic grunts beneath.",
                   "a drone and fifths; parallel parts; a high descant",
                   "Stillman, 'Himene Tahiti' (1993); McLean, Weavers of Song (1999)", "", 90.0,
                   { line ("grunts", "0:0.5 -:0.5 0:0.5 -:0.5 0:0.5 -:0.5 0:0.5 -:0.5 0:0.5 -:0.5 0:0.5 -:0.5 0:0.5 -:0.5 0:0.5 -:0.5"),
                     line ("drone", "7:8"), line ("melody", "12:2 14:1 12:1 16:2 12:2"), line ("descant", "19:2 21:2 19:4") } });
            add ({ "kroncong", "kroncong", P::meeting, "southeast asia", "Indonesia", "19th c.–", 1880, "functional",
                   "Portuguese-descended songs of Batavia grown into an Indonesian genre: a slow I–IV–V melody over cak and cuk (small "
                   "ukulele-like lutes) interlocking, a plucked cello, and the flute's obbligato.",
                   "I–IV–V and the secondary dominant; the interlocking cak and cuk",
                   "Becker, 'Kroncong, Indonesian Popular Music' (1975); Yampolsky, Music of Indonesia vol. 2", "", 80.0,
                   { line ("cello", "-12:1 -5:1 -12:1 -5:1 -7:1 0:1 -5:1 2:1"),
                     line ("cak", "4:0.5 7:0.5 4:0.5 7:0.5 4:0.5 7:0.5 4:0.5 7:0.5 5:0.5 9:0.5 5:0.5 9:0.5 2:0.5 7:0.5 2:0.5 5:0.5"),
                     line ("voice", "12:2 11:1 9:1 9:2 7:2") } });
            return h;
        }
    }

    const std::vector<Harmony>& harmonies()
    {
        static const std::vector<Harmony> all = build();
        return all;
    }

    int findHarmony (const std::string& plain)
    {
        const auto& all = harmonies();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].plain == plain)
                return (int) i;
        return -1;
    }

    std::string partTitle (Part p)
    {
        switch (p)
        {
            case Part::western: return "Western art music";
            case Part::jazz:    return "jazz";
            case Part::world:   return "the world";
            case Part::meeting: return "grown from meeting Western harmony";
        }
        return {};
    }

    namespace
    {
        const std::vector<std::pair<std::string, std::string>>& eras()
        {
            static const std::vector<std::pair<std::string, std::string>> all {
                { "middle ages", "Middle Ages" }, { "renaissance", "Renaissance" }, { "baroque", "Baroque" },
                { "common practice", "common practice" }, { "twentieth century", "twentieth century" } };
            return all;
        }
    }

    std::vector<std::string> groups (Part p)
    {
        std::vector<std::string> order;
        if (p == Part::western)
            for (const auto& [e, title] : eras())
                order.push_back (e);
        else if (p == Part::jazz)
            order.push_back ("");
        else
            order = mixer::regions();
        std::vector<std::string> out;
        for (const auto& g : order)
            if (! inGroup (p, g).empty())
                out.push_back (g);
        return out;
    }

    std::string groupTitle (Part p, const std::string& group)
    {
        if (p == Part::western)
            for (const auto& [e, title] : eras())
                if (e == group)
                    return title;
        return p == Part::jazz ? std::string() : mixer::regionTitle (group);
    }

    std::vector<int> inGroup (Part p, const std::string& group)
    {
        std::vector<int> out;
        const auto& all = harmonies();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].part == p && all[i].group == group)
                out.push_back ((int) i);
        // By tradition, the traditions in order of their first date, then as listed.
        std::map<std::string, int> first;
        for (auto i : out)
        {
            const auto& t = all[(size_t) i];
            auto it = first.find (t.tradition);
            if (it == first.end() || t.year < it->second)
                first[t.tradition] = t.year;
        }
        std::stable_sort (out.begin(), out.end(), [&] (int a, int b)
        {
            const auto& x = all[(size_t) a];
            const auto& y = all[(size_t) b];
            return std::tuple (first[x.tradition], x.tradition) < std::tuple (first[y.tradition], y.tradition);
        });
        return out;
    }

    double beats (const Voice& v)
    {
        double b = 0.0;
        for (const auto& n : v.notes)
            b += n.beats;
        return b;
    }

    bool valid (const Harmony& h)
    {
        if (h.voices.empty() || h.voices.size() > 15 || h.bpm <= 0.0)
            return false;
        const auto length = beats (h.voices.front());
        for (const auto& v : h.voices)
        {
            if (v.notes.empty() || std::abs (beats (v) - length) > 1.0e-9)
                return false;
            for (const auto& n : v.notes)
                if (n.beats <= 0.0 || (! n.rest && (n.pitch + 36 < 0 || n.pitch + 71 > 127)))
                    return false;
        }
        return length > 0.0;
    }

    midifile::SongPtr toSong (const Harmony& h, int root, int cycles, double bpm)
    {
        constexpr int tpq = 480;
        auto song = std::make_shared<midifile::Song>();
        song->format = 0;
        song->ticksPerQuarter = tpq;
        song->tempos.push_back ({ 0, 60.0e6 / std::max (1.0, bpm) });
        song->trackNames.push_back (h.name);
        cycles = std::max (1, cycles);
        const auto cycleTicks = h.voices.empty() ? (int64_t) 0 : (int64_t) std::llround (beats (h.voices.front()) * tpq);
        struct Timed { int64_t tick; int order; midifile::Event e; };   // offs (0) before ons (1) at the same tick
        std::vector<Timed> events;
        for (size_t v = 0; v < h.voices.size(); ++v)
        {
            // A channel a voice, past the drums' tenth.
            const auto channel = (uint8_t) (v < 9 ? v : v + 1);
            for (int c = 0; c < cycles; ++c)
            {
                double at = 0.0;
                for (const auto& n : h.voices[v].notes)
                {
                    const auto start = c * cycleTicks + (int64_t) std::llround (at * tpq);
                    at += n.beats;
                    if (n.rest)
                        continue;
                    const auto end = c * cycleTicks + (int64_t) std::llround (at * tpq) - 1;
                    const auto key = (uint8_t) std::clamp (root + n.pitch, 0, 127);
                    events.push_back ({ start, 1, { start, (uint8_t) (0x90 | channel), key, 80 } });
                    events.push_back ({ end, 0, { end, (uint8_t) (0x80 | channel), key, 0 } });
                    song->channelsUsed |= (uint16_t) (1u << channel);
                }
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
}
