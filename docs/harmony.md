# Harmony

The owner asked on 2026-10-29 for "harmony as a submenu of mode", meaning mode "in the broader
sense of 'a method or way' versus the more strict Western art music definition", to "map out the
many traditions of harmony that go beyond just the western art music tradition". They asked for
research into the types of harmony in Western art music, into what the ethnomusicological
literature says about harmony and harmony-like structures outside it, and into traditions that
took something from Western harmony and grew it into a system of their own. Jazz has its own
submenu. Shape-note singing, Black American choral harmony and folk traditions with their own
harmony practices were asked for by name.

Mode ▸ harmony (`Source/ui/HarmonySection.cpp`, data in `Source/mixer/Harmonies.cpp`) holds
four parts:

- **Western art music**, by era, from organum to spectralism.
- **jazz**, its own submenu.
- **The world**, by region, as Mode ▸ melody and tuning are. These are the folk and classical
  practices that ethnomusicology describes as multipart: drones, parallel and diaphonic singing,
  heterophony, interlocking and hocket, overtone singing, and stratified polyphony.
- **Grown from meeting Western harmony**, by region. These are hymnody, brass bands and records
  remade into systems of their own.

Choosing one opens a dialog with what it is, where it comes from, the intervals and chords it
rests on, and its sources. It writes a short example on the Mode menu's final, as a MIDI clip on
the selected mixer track or as a MIDI file, with one channel per voice. A few examples are heard
in their own tuning (a laras, Pythagorean, an echos, the 'Are'are's near-equal seven steps), and
the dialog can tune the track to it.

The examples are sketches that show the texture, not transcriptions. Many of these traditions
sing in tunings that 12-TET only approximates (Georgian, Sardinian and Bulgarian singing among
them), and some are improvised or orally varied every time.

## What the literature says

Western theory long treated harmony as a stack of consonances that move by rule. Studies of
multipart music elsewhere describe other priorities:

- **Seconds can be consonant.** In Shope diaphony and Lithuanian sutartinės, the beating major
  second is the sound that is sought (Messner; Račiūnaitė-Vyčinienė).
- **The texture can be many versions of one line.** Heterophony, as in the Arab takht, Jiangnan
  sizhu, Korean sinawi and Russian podgoloski, makes harmony from the places where those versions
  part.
- **Harmony can come from the sum of parts no one plays alone.** Examples are hocket and
  interlocking: Banda-Linda horns, Balinese kotekan, Andean sikuri and Shona mbira (Arom; Tenzer;
  Turino; Berliner).
- **Harmony can come from one sound's partials.** Examples are xöömei, the Xhosa bows' two roots
  and Bunun pasibutbut (Levin; Dargie).
- **Harmony can come in layers of density over a core.** The gamelan's stratified polyphony is
  the example (Sumarsam; Hood).
- **Harmony can be mapped by zones.** Nketia sorted Africa's multipart singing into areas of
  thirds, of fourths and fifths, and of octaves.

Jordania (*Who Asked the First Question?*, 2006) surveys vocal polyphony worldwide. Ahmedaja and
Haid's *European Voices* (2008–) covers Europe's multipart singing.

## Sources not yet confirmed

- The shō example is a cluster in the manner of an aitake, not one of the named ones. The
  pitches of each aitake are tabled in Momii (Music Theory Online 26.4, 2020).
- The Aka example names only the two parts that the sources name, the mòtángòlè and the ósêsê.
- The 'Are'are dyads follow Zemp's account of paired pipes in an equiheptatonic tuning. The exact
  intervals were not checked against his transcriptions.

## Western art music

### Middle Ages

- **parallel organum** (Musica enchiriadis, c. 900; parallel). The first written polyphony: a chant (vox principalis) doubled a fourth or fifth below by the vox organalis, the doubling bent at the edges of the phrase to avoid the tritone, and both voices may be doubled at the octave. *Sonorities:* perfect fourths, fifths and octaves in parallel; unison at the start and end. *Sources:* Musica enchiriadis and Scolica enchiriadis (c. 900); Grout & Palisca. *Heard in:* pythagorean.
- **organum purum** (Notre-Dame school, 1160s–1250; drone). Léonin's two-voice organum for the Magnus liber organi: the chant's notes stretched into a long held tenor, over which the duplum spins melismas, meeting it on fifths, octaves and unisons and passing through dissonance between. *Sonorities:* perfect consonances on the tenor's notes; seconds, thirds and sixths passing. *Sources:* Magnus liber organi; Anonymous IV (c. 1275); Roesner, Le Magnus liber organi de Notre-Dame. *Heard in:* pythagorean.
- **english discant and gymel** (England, 13th–15th c.; parallel). The English taste for imperfect consonances: two voices moving in thirds (gymel, 'twin song') or three in chains of sixth-chords, heard on the Continent as the contenance angloise of Dunstable. *Sonorities:* parallel thirds and sixths, cadencing to the fifth and octave. *Sources:* Sumer is icumen in (13th c.); Bukofzer, Studies in Medieval and Renaissance Music; Martin le Franc, Le champion des dames.

### Renaissance

- **fauxbourdon** (Guillaume Du Fay (1397–1474), c. 1430; parallel). A chant in the top voice, a tenor a sixth below it, and an unwritten middle voice sung a fourth below the top: chains of 6/3 chords that open out to an octave and fifth at the cadence. *Sonorities:* 6/3 chords in parallel; the double leading-tone cadence to 8/5. *Sources:* Du Fay, Missa Sancti Jacobi (c. 1427); Trowell, 'Faburden and fauxbourdon'.
- **modal counterpoint** (Giovanni Pierluigi da Palestrina (1525–1594), 16th c.; counterpoint). Independent voices in a church mode, every dissonance prepared and resolved: the passing tone on the weak beat, the suspension on the strong one, the cadence on the octave or a full triad. Codified by Zarlino and, later, Fux's species. *Sonorities:* triads and their inversions; the 4–3 and 7–6 suspension; no parallel fifths or octaves. *Sources:* Zarlino, Le istitutioni harmoniche (1558); Fux, Gradus ad Parnassum (1725); Jeppesen, Counterpoint.

### Baroque

- **basso continuo and figured bass** (Italy, c. 1600–1750; chordal). A bass line with figures, realized at sight by keyboard or lute as chords above it: harmony thought of from the bass up, the seed of functional harmony and of the chord as a thing in itself. *Sonorities:* 5/3 and 6/3 chords, 6/4 and 7 by figure; the cadential 6/4–5/3. *Sources:* Viadana, Cento concerti ecclesiastici (1602); Heinichen, Der General-Bass (1728); Arnold, The Art of Accompaniment from a Thorough-Bass.

### common practice

- **functional harmony** (Jean-Philippe Rameau (1683–1764), 1722; functional). Major and minor keys in which every chord has a function, tonic, subdominant or dominant, and the fundamental bass moves by fifths toward the tonic: the harmony of Bach's chorales, Haydn, Mozart and Beethoven. *Sonorities:* triads and the dominant seventh; I–IV–V–I; the perfect and the deceptive cadence. *Sources:* Rameau, Traité de l'harmonie (1722); Riemann, Vereinfachte Harmonielehre (1893); Aldwell & Schachter, Harmony and Voice Leading.
- **neo-riemannian transformations** (Franz Liszt (1811–1886), 19th c.; chordal). Triads joined not by function but by smooth voice leading: P (parallel), L (leading-tone exchange) and R (relative) each move one note, so chains like the hexatonic cycle drift through distant keys. Named after Hugo Riemann by Cohn and Lewin. *Sonorities:* major and minor triads a third apart; the hexatonic and octatonic cycles. *Sources:* Cohn, 'Introduction to Neo-Riemannian Theory' (1998); Lewin, Generalized Musical Intervals and Transformations (1987).
- **romantic chromaticism** (Richard Wagner (1813–1883), 1859; functional). Keys stretched by chromatic voice leading until the tonic is implied more than stated: augmented sixths, half-diminished sevenths that do not resolve, and the Tristan chord. *Sonorities:* the Tristan chord (F–B–D♯–G♯), the augmented sixth, the diminished seventh, chromatic mediants. *Sources:* Wagner, Tristan und Isolde (1859); Kurth, Romantische Harmonik (1920); Bailey (ed.), Prelude and Transfiguration.

### twentieth century

- **impressionist planing** (Claude Debussy (1862–1918), c. 1890–1915; parallel). Chords moved in parallel as a single colour, the dominant ninth that does not resolve, whole-tone and pentatonic fields: harmony as timbre rather than progression. *Sonorities:* parallel ninth chords; whole-tone and pentatonic sonorities. *Sources:* Debussy, La cathédrale engloutie, Nuages; Lockspeiser, Debussy; DeVoto, Debussy and the Veil of Tonality.
- **the mystic chord** (Alexander Scriabin (1872–1915), 1910; chordal). A six-note chord of fourths (augmented, diminished and perfect) that stands as a centre in its own right, from which the melodies of Prometheus are drawn. *Sonorities:* C–F♯–B♭–E–A–D, a subset of the acoustic scale. *Sources:* Scriabin, Prometheus: The Poem of Fire, Op. 60 (1910); Perle, 'Scriabin's Self-Analyses'.
- **free atonality and twelve-tone harmony** (Arnold Schoenberg (1874–1951), 1908–1923; chordal). Harmony emancipated from tonal resolution: chords chosen by interval content (pitch-class sets), and in the twelve-tone method drawn from the segments of a row. *Sonorities:* set classes such as 3-5 and 4-19; the all-interval tetrachords; row segments as chords. *Sources:* Schoenberg, Harmonielehre (1911); Forte, The Structure of Atonal Music (1973).
- **polytonality** (Igor Stravinsky (1882–1971), 1911; chordal). Two keys at once: the Petrushka chord sets C major against F♯ major, a tritone apart; Milhaud and Ives took it further. The octatonic scale often holds both. *Sonorities:* two triads a tritone apart (C and F♯); octatonic collections. *Sources:* Stravinsky, Petrushka (1911); Milhaud, 'Polytonalité et atonalité' (1923); van den Toorn, The Music of Igor Stravinsky.
- **quartal harmony** (Paul Hindemith (1895–1963), 1937; chordal). Chords stacked in fourths instead of thirds, with no root by default: in Schoenberg's Chamber Symphony, Bartók, Hindemith's theory of chord values, and from there jazz. *Sonorities:* stacked perfect fourths; the fourth chord and its inversions. *Sources:* Schoenberg, Kammersymphonie Op. 9 (1906); Hindemith, Unterweisung im Tonsatz (1937); Persichetti, Twentieth-Century Harmony (1961).
- **pandiatonicism** (Aaron Copland (1900–1990), 1930s–1940s; chordal). Every note of a diatonic scale free to sound with every other, without functional progression: the open, added-note sound of Copland's Appalachian Spring and Stravinsky's neoclassicism. Slonimsky's term. *Sonorities:* triads with added seconds, fourths and sixths; diatonic clusters. *Sources:* Slonimsky, Music since 1900 (1937); Copland, Appalachian Spring (1944).
- **minimalist harmony** (Steve Reich (b. 1936), 1960s–; chordal). Slow harmonic rhythm over pulsing patterns: a chord, or a cycle of a few, held while the texture changes inside it, as in Reich's Music for 18 Musicians and Glass's arpeggiated progressions. *Sonorities:* diatonic chords with added notes, changing slowly; static drones. *Sources:* Reich, Music for 18 Musicians (1976); Glass, Einstein on the Beach (1976); Potter, Four Musical Minimalists.
- **spectral harmony** (Gérard Grisey (1946–1998), 1975; overtone). Chords made from the partials of a sound, its harmonic spectrum or an analysed one, heard as a single timbre; the music of L'Itinéraire, Grisey's Partiels and Murail's Gondwana. *Sonorities:* the harmonic series, its seventh and eleventh partials rounded here to the nearest key. *Sources:* Grisey, Partiels (1975); Murail, 'The Revolution of Complex Sounds' (2005); Fineberg, 'Guide to the Basic Concepts of Spectral Music'.

## jazz

- **twelve-bar blues** (the blues, c. 1900–; functional). Twelve bars on I, IV and V, each a dominant seventh, the blue third and seventh sung against them: the dominant seventh as a tonic, the root of jazz harmony. *Sonorities:* I7, IV7, V7; the blue notes ♭3, ♭5, ♭7 against major chords. *Sources:* Handy, St. Louis Blues (1914); Titon, Early Downhome Blues; Gioia, The History of Jazz.
- **extended tertian chords** (swing, 1930s; chordal). Sevenths as the norm and ninths, elevenths and thirteenths added above: the sixth and ninth chords of swing sections, voiced close in the saxophones and brass. *Sonorities:* 6, maj7, 9, 11, 13 chords; the four-way close voicing. *Sources:* Levine, The Jazz Theory Book (1995); Ellington, Mood Indigo (1930).
- **ii–v–i and tritone substitution** (bebop, 1940s; functional). The ii–V–I as the unit of a tune, and the dominant replaced by the one a tritone away, which shares its third and seventh, giving the chromatic bass of bebop: Dm7–D♭7–Cmaj7. *Sonorities:* m7–7–maj7; the tritone substitute ♭II7; altered dominants (♭9, ♯9, ♯11, ♭13). *Sources:* Levine, The Jazz Theory Book; Parker & Gillespie, Groovin' High (1945); Owens, Bird: The Life and Music of Charlie Parker.
- **reharmonization and block chords** (George Shearing (1919–2011), 1949; parallel). A melody harmonized note by note, the melody doubled an octave below with a chord in between ('locked hands'), passing notes taken as diminished sevenths. *Sonorities:* drop-2 and locked-hands voicings; the diminished-seventh passing chord. *Sources:* Shearing, September in the Rain (1949); Levine, The Jazz Piano Book.
- **rootless voicings** (Bill Evans (1929–1980), 1958–; chordal). The left hand leaves the root to the bass and plays third, seventh and colour tones, in two shapes (A and B) that move by step through a ii–V–I. *Sonorities:* 3–5–7–9 and 7–9–3–5; G13 as F–A–B–E. *Sources:* Levine, The Jazz Piano Book (1989); Evans, Portrait in Jazz (1959).
- **modal and quartal jazz** (Miles Davis and Bill Evans, 1959; chordal). A mode held for many bars instead of changing chords, voiced in fourths: the 'So What' chord (three fourths and a major third) and McCoy Tyner's fourths. *Sonorities:* the So What chord; stacked fourths moved in parallel within the dorian mode. *Sources:* Davis, Kind of Blue (1959); Russell, The Lydian Chromatic Concept of Tonal Organization (1953).
- **coltrane changes** (John Coltrane (1926–1967), 1959; functional). Three keys a major third apart, each reached by its own dominant, dividing the octave in three: Giant Steps' B–D7–G–B♭7–E♭. *Sonorities:* maj7 chords a major third apart, each with its V7. *Sources:* Coltrane, Giant Steps (1960); Demsey, John Coltrane Plays Giant Steps.
- **upper-structure triads** (post-bop, 1960s–; chordal). A triad played above a dominant's tritone so that its notes are the tensions: D major over C7 gives 9, ♯11 and 13; A♭ over C7 gives ♭13, root and ♯9. *Sonorities:* a major triad over the 3rd and 7th of a dominant; the altered scale. *Sources:* Levine, The Jazz Piano Book; Mehegan, Jazz Improvisation (1959).

## the world

### Europe

- **georgian polyphony** (Georgia, first written 11th c.; polyphony). Three-voice singing, in the east over a held bass drone and in the west (Guria, Svaneti) in three independent parts, with sharp seconds, fourths and sevenths resolving to the fifth-and-octave, in a tuning of its own. UNESCO 2001. *Sonorities:* fifth plus octave (or fourth) at cadences; seconds, fourths and sevenths in passing; a non-tempered scale. *Sources:* Jordania, Who Asked the First Question? (2006); Erkomaishvili archive; Scherbaum et al. on Georgian tuning.
- **byzantine ison** (Byzantine and Greek Orthodox chant, 15th c.–; drone). The chant sung over the ison, a held note on the base of the echos, moving with the melody's centre. *Sonorities:* the melody against its drone: unisons, fourths and fifths at rest, every interval of the echos in passing. *Sources:* Levy & Troelsgård, 'Byzantine chant' (Grove); Conomos. *Heard in:* echos protos.
- **albanian iso-polyphony** (Albania (Tosk and Lab); drone). Two or three soloists, the 'taker' and the 'turner', in an anhemitonic pentatonic, over the iso, a drone sung by the group on a vowel. UNESCO 2005. *Sonorities:* seconds and fourths between soloists; the drone under both. *Sources:* Tole, Albanian Folk Iso-polyphony (2008); Ahmedaja, European Voices I.
- **shope diaphony** (Bulgaria (Shopluk); drone). Two-part singing in which one voice holds a drone and the other moves close above and below it, prizing the beating major second as the most beautiful sound. *Sonorities:* the major second against the drone; unisons at the ends. *Sources:* Messner, Die Schwebungsdiaphonie in Bistrica (1980); Rice, May It Fill Your Soul.
- **paghjella** (Corsica; polyphony). Three men's voices: the seconda gives the melody, the bassu supports it, the terza decorates high above with melismas; sung in a close huddle. UNESCO 2009. *Sonorities:* fifths and octaves at cadences, thirds and seconds between. *Sources:* Bithell, Transported by Song: Corsican Voices (2007).
- **sutartinės** (Lithuania; polyphony). Women's multipart songs in which voices meet in parallel and crossing seconds, the seconds heard as consonant; two-part, three-part canons, and four-part as two pairs. UNESCO 2010. *Sonorities:* major seconds as the stable interval. *Sources:* Račiūnaitė-Vyčinienė, Sutartinės: Lithuanian Polyphonic Songs (2002).
- **russian village polyphony** (Russia and Ukraine; heterophony). Protyazhnaya (drawn-out) songs where the voices branch from the tune as podgoloski, 'under-voices', each a variant of it, converging on the unison or octave at the ends of phrases. *Sonorities:* unison and octave at cadences; thirds, fourths and fifths where the variants part. *Sources:* Lineva, Great Russian Songs in Folk Harmonization (1904); Mazo; Ivanytsky on Ukrainian polyphony.
- **cantu a tenore** (Sardinia; chordal). Four men: the boghe leads, the bassu and contra answer in guttural voices a fifth apart, the mesu boghe ornaments above; the chord is built on the major triad. UNESCO 2005. *Sonorities:* root, fifth and octave from the bassu, contra and mesu boghe; the boghe's melody above. *Sources:* Lortat-Jacob, Chants de Passion (1998); Macchiarella.
- **seto leelo** (Seto (Estonia); polyphony). A lead singer gives each line and the choir takes it up in two parts: the low torrõ, many voices, and the single high killõ above, meeting in seconds, fourths and fifths. UNESCO 2009. *Sonorities:* the killõ a second, fourth or fifth above the torrõ; unison endings. *Sources:* Pärtlas, 'Seto Polyphonic Song' (2006).
- **naturjodel** (Alpine (Switzerland and Austria); chordal). Wordless yodels flipping between chest and head voice, sung in two to four parts in thirds and sixths over a bass on tonic and dominant, sometimes with the natural, sharpened fourth of the alphorn. *Sonorities:* thirds and sixths over I and V; the alphorn fa (an 11th partial). *Sources:* Baumann, Das sogenannte Alphorn-Fa (1976); Wise, Yodel in Switzerland.
- **klapa** (Dalmatia (Croatia), 19th c.–; chordal). Small groups of men (now women too) singing a cappella in close tertian harmony: first tenor leads, second tenor and baritone fill in, the bass holds the root, the chords lingering and swelling. UNESCO 2012. *Sonorities:* close triads and dominant sevenths; the lead's melody over a sustained bass. *Sources:* Ćaleta, 'Klapa singing' (1997).

### West Asia

- **takht heterophony** (Egypt and the Levant; heterophony). The oud, qānūn, nāy and violin of a takht play one melody in a maqām each in their own way, at once, with the singer: harmony as many versions of one line. *Sonorities:* the unison and octave, and the passing seconds where the versions differ. *Sources:* Racy, Making Music in the Arab World (2003); Marcus, Music in Egypt. *Heard in:* rast.

### Central Asia

- **dombra küi** (Kazakhs; parallel). The two-stringed dombra tuned a fourth or fifth apart, the lower string a drone or moving in parallel with the upper: the harmony of the instrumental küi. *Sonorities:* fourths and fifths between the strings. *Sources:* Utegalieva; Levin, The Hundred Thousand Fools of God.
- **urtyn duu with the morin khuur** (Mongolia; heterophony). The long song, free in time, and the horse-head fiddle that follows it a little behind or ahead, ornamenting the same line. UNESCO 2005. *Sonorities:* unisons and octaves, seconds in passing. *Sources:* Pegg, Mongolian Music, Dance, and Oral Narrative (2001).
- **xöömei** (Tuva and Mongolia; overtone). One singer, two parts: the fundamental held as a drone while the throat and mouth pick out its overtones, the 6th to the 12th, as a melody. Mongolian khöömii, UNESCO 2010. *Sonorities:* a drone and its harmonics: octave, fifth, major third and the flat seventh of nature. *Sources:* Levin & Süzükei, Where Rivers and Mountains Sing (2006).

### South Asia

- **tānpurā drone** (Hindustani and Karnatak; drone). The tānpurā's sa and pa sounding all the time, the melody heard against it: each svara a consonance or a tension with the drone and its overtones, and the rāga's character in how it sits there. *Sonorities:* the drone's sa, pa (or ma, or ni) and their harmonics against each svara. *Sources:* Jairazbhoy, The Rāgs of North Indian Music (1971); Raman on the tanpura's jawari.

### Southeast Asia

- **gamelan layering** (Java; stratified). Stratified polyphony: a core melody (balungan) on the sarons, the same line elaborated faster and higher by the peking and bonangs, and punctuated by kenong and gong at the ends of its phrases (colotomy); octaves and the kempyung (a gap of two keys) sound together. *Sonorities:* octaves (gembyang) and kempyung; the gong at the end of the phrase. *Sources:* Sumarsam, Gamelan (1995); Becker, Traditional Music in Modern Java; Hood, The Nuclear Theme as a Determinant of Patet. *Heard in:* slendro.
- **khaen** (Lao; drone). The free-reed mouth organ of the Lao, playing melody over its own held drones and in parallel octaves, fourths and fifths, in the lai modes. UNESCO 2017. *Sonorities:* drones on the tonic and fifth; melody doubled a fourth below. *Sources:* Miller, Traditional Music of the Lao (1985).
- **kotekan** (Bali, gong kebyar, 1910s; interlocking). Two parts, polos and sangsih, each playing only some of the notes of a fast figuration that only sounds whole together; the paired instruments tuned apart so their unisons shimmer (ombak). *Sonorities:* the interlocked line; seconds and thirds where the parts overlap. *Sources:* Tenzer, Gamelan Gong Kebyar (2000); McPhee, Music in Bali (1966).

### East Asia

- **shō clusters (aitake)** (Japanese court (gagaku), 8th c.–; cluster). The mouth organ of gagaku holds a cluster of five or six pipes, an aitake named for its lowest note, and slides between them; the hichiriki and ryūteki play the melody heterophonically through it. (The cluster here is in the manner of an aitake, not a particular one.) *Sonorities:* clusters of seconds, fourths and fifths. *Sources:* Momii, 'A Transformational Approach to Gesture in Shō Performance', Music Theory Online 26.4 (2020); Garfias, Music of a Thousand Autumns.
- **grand song** (Dong (Guizhou); drone). Kgal laox, sung without conductor or instruments: many voices hold and sway on a low part while one or two soloists sing the high part, imitating birds and cicadas. UNESCO 2009. *Sonorities:* seconds, thirds and fourths over the low held part. *Sources:* Ingram, Singing the Dong Grand Song (2012).
- **upopo rounds** (Ainu (Hokkaido); polyphony). Women seated round a lacquered chest sing upopo as ukouk: the same short phrase in canon, entering one after another, so the round makes the harmony. UNESCO 2009 (Ainu traditional dance). *Sonorities:* the canon's overlaps: seconds, fourths and fifths. *Sources:* Tanimoto Kazuyuki; Chiba, Ainu Music.
- **pasibutbut** (Bunun (Taiwan); chordal). The prayer for millet: men in a circle sustain a chord of octave, fifth and third and raise it slowly, in tiny steps, the higher it reaches the better the harvest. *Sonorities:* root, fifth, octave and major third, rising. *Sources:* Kurosawa (1943); Wu Rung-shun.
- **sizhu heterophony** (Jiangnan (Shanghai); heterophony). Silk-and-bamboo ensembles: erhu, dizi, pipa, yangqin and xiao each realize the same skeletal tune, like Lao liuban, with their own density and ornament. *Sonorities:* unisons, octaves and passing seconds between versions. *Sources:* Witzleben, 'Silk and Bamboo' Music in Shanghai (1995).
- **sinawi** (Korea (Jeolla); heterophony). From shaman ritual music: piri, daegeum, haegeum and ajaeng improvise at once in one mode, close and apart, over a janggu rhythm: a deliberate 'disorder' that is the point. *Sonorities:* unisons and octaves pulled apart into seconds and fourths. *Sources:* Howard, Korean Music: A Listening Guide; Lee Byong-won.

### Africa

- **aka and baka polyphony** (Aka and Baka (Central Africa); polyphony). Contrapuntal singing in four parts on short ostinatos, each varied and yodelled, the main tune (mòtángòlè) and its response (ósêsê) and two more, overlapping into a dense pentatonic weave. UNESCO 2003. *Sonorities:* pentatonic unisons, fourths, fifths and octaves in constantly changing combinations. *Sources:* Arom, African Polyphony and Polyrhythm (1991); Fürniss, 'What is a part?' (2016).
- **horn hocket** (Banda-Linda (Central African Republic); interlocking). An orchestra of horns, each playing one or two pitches at its own moment, so that melody and chords come only from their sum. *Sonorities:* the composite line; fourths and octaves where horns coincide. *Sources:* Arom, African Polyphony and Polyrhythm (1991).
- **akan thirds** (Akan (Ghana); parallel). Call and response with the chorus in parallel thirds; Nketia mapped Africa's harmony in zones, here the thirds, elsewhere fourths, fifths and octaves. *Sonorities:* parallel thirds, at cadences the unison or octave. *Sources:* Nketia, The Music of Africa (1974).
- **dorze polyphony** (Dorze (Ethiopia); polyphony). Multipart singing in up to six parts, each with its name and role, on a pentatonic base, overlapping and hocketing, unlike the monophony around it in Ethiopia. *Sonorities:* pentatonic fourths, fifths and seconds between interwoven parts. *Sources:* Lortat-Jacob, 'Polyphonies dorzé' (Ethiopia: Polyphonies et techniques vocales, 1994).
- **mbira interlocking** (Shona (Zimbabwe); interlocking). Two mbira dzavadzimu: kushaura leads and kutsinhira interlocks, each part itself in two hands, in a cycle of four phrases of twelve pulses whose dyads give a harmonic progression. *Sonorities:* dyads of fourths, fifths and thirds, cycling. *Sources:* Berliner, The Soul of Mbira (1978); Tracey, 'The system of the mbira' (1989).
- **two-root harmony** (Xhosa (South Africa); overtone). From the musical bows, uhadi and umrhubhe: two fundamentals a whole tone apart, each with its own overtones, alternate as two 'chords'; the songs, and split-tone umngqokolo singing, follow them. *Sonorities:* two roots a whole tone apart, each with its octave, fifth and major third. *Sources:* Dargie, Xhosa Music: Its Techniques and Instruments (1988).

### the Americas

- **sikuri** (Aymara (Andes); interlocking). Panpipes in halves, ira and arka, that alternate notes to make one melody; played in bands with pipes in octaves and fifths, giving parallel harmony. *Sonorities:* the hocket of the halves; parallel octaves and fifths between sizes. *Sources:* Turino, Moving Away from Silence (1993).

### Oceania

- **yiḏaki and manikay** (Yolŋu (Arnhem Land); drone). Song (manikay) over the yiḏaki's drone and its overblown 'toot', with clapsticks: the voice falls through its range against the unchanging fundamental. *Sonorities:* the drone and its overblown upper note against a descending line. *Sources:* Knopoff, 'Yolŋu clan songs'; Toner.
- **'are'are panpipes** ('Are'are (Solomon Islands); polyphony). Panpipe ensembles, and solo pipes built so two adjacent tubes sound together, in a nearly equal heptatonic tuning: two or three kinds of dyad within each piece. *Sonorities:* dyads of neighbouring pipes in seven near-equal steps. *Sources:* Zemp, ''Are'are Classification of Musical Types and Instruments' (1978); Rahn (2015, 2023). *Heard in:* 7 equal steps.

## grown from meeting Western harmony

### Europe

- **bulgarian women's choir** (Bulgaria, 1951; chordal). Filip Kutev's state ensemble set village songs for women's choir, keeping the open-throated sound and the drone and seconds of diaphony inside composed, often chromatic, harmony: Le Mystère des Voix Bulgares. *Sonorities:* drones with clustered seconds; parallel and crossing parts; composed chromatic chords. *Sources:* Buchanan, Performing Democracy (2006); Rice, May It Fill Your Soul (1994).

### Southeast Asia

- **kroncong** (Indonesia, 19th c.–; functional). Portuguese-descended songs of Batavia grown into an Indonesian genre: a slow I–IV–V melody over cak and cuk (small ukulele-like lutes) interlocking, a plucked cello, and the flute's obbligato. *Sonorities:* I–IV–V and the secondary dominant; the interlocking cak and cuk. *Sources:* Becker, 'Kroncong, Indonesian Popular Music' (1975); Yampolsky, Music of Indonesia vol. 2.

### Africa

- **highlife** (Ghana, 1920s–; functional). Dance-band and guitar music that took I, IV and V from brass bands and hymns and set them in cycles over a timeline, the guitar's thirds and sixths in two interlocking parts. *Sonorities:* I–IV–I–V cycles; parallel thirds and sixths. *Sources:* Collins, Highlife Time (1996); Nketia.
- **makwaya and isicathamiya** (Zulu and South African choirs, 1939; chordal). Mission hymns remade: makwaya choirs, and isicathamiya's men's a cappella, with a call from the lead, a heavy bass and the chorus answering on I, IV and I6/4–V: Solomon Linda's Mbube, Ladysmith Black Mambazo. *Sonorities:* I–IV–I6/4–V; the deep bass and close upper parts. *Sources:* Erlmann, Nightsong (1996); Linda, Mbube (1939).
- **congolese rumba** (Congo, 1940s–; interlocking). Cuban son records reheard in Léopoldville and Brazzaville, then guitars in interlocking parts on short I–IV–V cycles, the voices in thirds: Franco, Tabu Ley, soukous. UNESCO 2021. *Sonorities:* I–IV–V cycles; parallel thirds in the voices and guitars. *Sources:* Stewart, Rumba on the River (2000); White, Rumba Rules (2008).

### the Americas

- **lining out (dr. watts hymnody)** (Black American choral harmony, 18th c.–; heterophony). A leader lines out each line of an Isaac Watts hymn, and the congregation sings it back slowly, everyone ornamenting the tune at once: a heterophony thick enough to be heard as harmony. *Sonorities:* unisons and octaves spread into passing seconds, thirds and fourths. *Sources:* Dargan, Lining Out the Word (2006); Southern, The Music of Black Americans.
- **concert spiritual** (Black American choral harmony, 1871; functional). The Fisk Jubilee Singers took the spirituals into four-part concert arrangements; Burleigh, Dett and Hall Johnson made them art song and choral repertory, keeping the pentatonic tunes and the call and response. *Sonorities:* four-part hymn harmony on pentatonic tunes; the plagal cadence. *Sources:* Marsh, The Story of the Jubilee Singers (1875); Burleigh, Deep River (1916); Graham, Spirituals and the Birth of a Black Entertainment Industry.
- **jubilee and gospel quartet** (Black American choral harmony, 1930s; chordal). Four or five men, the lead in the middle, the bass a rhythm instrument of its own: the jubilee style of the Golden Gate Quartet and the hard gospel of the Soul Stirrers, ancestor of doo-wop and soul. *Sonorities:* close dominant sevenths and sixths; the bass's walking lines. *Sources:* Seroff; Lornell, Happy in the Service of the Lord (1988).
- **gospel choir harmony** (Black American choral harmony, 1930s–; functional). Thomas A. Dorsey's gospel songs and the choirs that came after: three-part soprano, alto and tenor over piano and organ, passing diminished chords, the IV–I 'amen' and the walk-ups of the bass. *Sonorities:* triads in close position in three parts; the ♯iv°7 passing chord; the plagal cadence. *Sources:* Boyer, How Sweet the Sound (1995); Harris, The Rise of Gospel Blues (1992).
- **sacred harp (four shapes)** (Appalachian shape-note singing, 1844; polyphony). Fasola singing from The Sacred Harp: four parts in a hollow square, the tune in the tenor, the treble and tenor doubled by men and women, open fifths and octaves, parallel fifths unashamed, chords often without thirds. *Sonorities:* open fifths and octaves; final chords without a third; dispersed harmony. *Sources:* White & King, The Sacred Harp (1844); Cobb, The Sacred Harp: A Tradition and Its Music; Miller, Traveling Home.
- **southern harmony and christian harmony** (Appalachian shape-note singing, 1835–1866; polyphony). William Walker's tunebooks: The Southern Harmony (1835) in four shapes and three parts, and The Christian Harmony (1866) in seven shapes, still sung in the North Carolina and Alabama mountains; the same dispersed, fifth-rich harmony. *Sonorities:* three parts in open fifths; the seven-shape do-re-mi. *Sources:* Walker, The Southern Harmony (1835), The Christian Harmony (1866); Jackson, White Spirituals in the Southern Uplands (1933).
- **seven-shape gospel** (Appalachian shape-note singing, 1870s–; functional). The seven-shape music of the Ruebush–Kieffer and James D. Vaughan publishers and the singing schools: functional gospel harmony with a moving bass, the root of Southern gospel quartets. *Sonorities:* I, IV, V7 and secondary dominants; the bass answering the upper voices. *Sources:* Aikin, The Christian Minstrel (1846); Harrison, 'Shape-note singing' (Grove); Goff, Close Harmony (2002).
- **duets in thirds** (Mexico and the Andes, 19th c.–; parallel). Two voices a third (or sixth) apart through the whole song, over guitar or harp chords on I, IV and V: the canción ranchera and huapango duos, the Andean huayno, and much of Latin American folk song from Spanish roots. *Sonorities:* parallel thirds and sixths; I–IV–V. *Sources:* Sheehy, Mariachi Music in America (2006); Turino.
- **barbershop** (United States, 1890s–; chordal). Four voices in close harmony, the lead below the tenor, moving round the circle of fifths on dominant sevenths that 'ring' when tuned just, so their overtones lock. *Sonorities:* the barbershop seventh; the circle of fifths; just-tuned major triads. *Sources:* Abbott, 'Play That! Barber Shop Harmony' (1992); Averill, Four Parts, No Waiting (2003).
- **bossa nova** (Brazil, 1958; chordal). Samba slowed and quieted, with jazz's sevenths, ninths and chromatic voice leading in the guitar's inner voices: Jobim and João Gilberto. *Sonorities:* maj7, m7, 7 with 9 and 13; II7 and ♭II7. *Sources:* Jobim, Garota de Ipanema (1962); Castro, Bossa Nova (2000).

### Oceania

- **hīmene tārava** (Tahiti, 19th c.–; polyphony). Hymn singing taken from the missionaries and made Polynesian: large choirs in many parts, a drone, a high descant, and the men's rhythmic grunts beneath. *Sonorities:* a drone and fifths; parallel parts; a high descant. *Sources:* Stillman, 'Himene Tahiti' (1993); McLean, Weavers of Song (1999).
