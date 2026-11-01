<!--
Composition: an entry for every item of the Compose menu (Source/ui/ComposeSection.cpp,
fillComposeMenu, and the methods of Source/mixer/Compose.cpp), in the menu's order: the score and
its notations, Architectonics, then the methods by region, oldest first, and the Rhythm Generator.
Each says what the method is and its history, then the books and links; a "phi:" link at the end
opens the tool itself, by its command id (ComposeSection.cpp). How to use the tool
is the manual's (docs/compose.md and the pages it names), not this file's.

The sources are those each method names in Compose.cpp, and docs/compose.md, docs/serial.md,
docs/stochastic.md, docs/cubify.md, docs/architectonics.md, docs/bol-processor.md and
docs/rhythms.md. Written 2026-11-01; the four entries this group had in references.md moved here.
-->

# Composition

## Composing by method

Composing by method is writing music by following a procedure: rules, a plan or chance worked
out step by step. Every tradition has rules for making music, and many have methods that a
musician can follow almost mechanically.

Guido of Arezzo gave a way to draw a chant melody from the vowels of its text (*Micrologus*,
c. 1026). Isorhythm and the canon are older procedures of this kind, and so are the dice games
of the eighteenth century, such as the *Musikalisches Würfelspiel* published under Mozart's name
in 1792. In the twentieth century the twelve-tone row, Cage's chance operations and Xenakis's
probabilities made the method part of the work. Lejaren Hiller and Leonard Isaacson's *Illiac
Suite* for string quartet (1957) was the first music composed by computer. It began with Fux's
counterpoint rules and ended with Markov chains. Gottfried Michael Koenig's Project 1 (1964) and
David Cope's Experiments in Musical Intelligence (from 1981) came after it. The Compose menu sets
methods from many traditions side by side. Some come from a written theory. Others are a
description, by those who studied them, of how a living practice unfolds.

- [Algorithmic composition](https://en.wikipedia.org/wiki/Algorithmic_composition), Wikipedia.
- Gerhard Nierhaus, *Algorithmic Composition: Paradigms of Automated Music Generation*
  (Springer, 2009).
- Lejaren Hiller and Leonard Isaacson, *Experimental Music: Composition with an Electronic
  Computer* (McGraw-Hill, 1959).
- Curtis Roads, *The Computer Music Tutorial* (MIT Press, 1996), part on algorithmic composition.

In PHI: [Compose > Compose...](phi:compose.dialog)

## Microtonal notation

Staff notation has to bend for music between the twelve semitones. Writers since Antiquity have
used signs for the smaller steps. Western staff notation has added them since the eighteenth
century. In the 1920s Alois Hába and Ivan Wyschnegradsky wrote quarter-tone music with signs
of their own, and quarter-tone sharps and flats became the most common of them. Just intonation took a sign for each prime: Ben Johnston's notation (1960s), and the
Helmholtz-Ellis accidentals of Marc Sabat and Wolfgang von Schweinitz (2000s). Sagittal (George
Secor and Dave Keenan, from 2001) gives one family of arrow signs for any tuning, and the
Functional Just System (FJS) names just intervals by their primes with one rule. PHI's staff draws its signs from Xen Tuner's notations, which hold many
of these.

- [Microtonal music](https://en.wikipedia.org/wiki/Microtonal_music), Wikipedia.
- [Sagittal](https://sagittal.org), the system's own site.
- Marc Sabat and Wolfgang von Schweinitz, "The Extended Helmholtz-Ellis JI Pitch Notation"
  (Plainsound Music Edition, 2004).
- Easley Blackwood, *The Structure of Recognizable Diatonic Tunings* (Princeton, 1985).

In PHI: [Compose > New Microtonal Score...](phi:compose.newScore)

# Composition / Notations

## Notation in each tradition

Many musics were never written on the European staff, and the ones that were written had their
own scripts, made for their own instruments and modes: tablatures, syllables, numbers and grids.
Most were written to remember or to teach, not to compose from, and the player brings much that
is not on the page. In the nineteenth and twentieth centuries several were made or remade on the
model of the staff or of the French cipher notation, often as part of a nation's reform of its
music teaching. The entries in this group are the notations of Compose > New Score In.

- [Musical notation](https://en.wikipedia.org/wiki/Musical_notation), Wikipedia.
- Ter Ellingson, "Notation", in Helen Myers (ed.), *Ethnomusicology: An Introduction* (Norton,
  1992).

## Hindustani sargam (Bhatkhande)

Hindustani notation writes the sargam syllables (Sa Re Ga Ma Pa Dha Ni) by their initials, with
signs for the flat (*komal*) and sharp (*tīvra*) notes and for the lower and upper octaves. The
syllables are written in a grid of the tāl's beats (*mātrās*), with the tāl's divisions marked:
sam, tālī and khālī.

V. N. Bhatkhande (1860–1936) set the system out while he collected compositions from the
gharānās, and printed them in this notation in his *Kramik Pustak Mālikā* (six volumes). Vishnu Digambar Paluskar's rival notation of the same years is still used by some
teachers. See [V. N. Bhatkhande](research:V.%20N.%20Bhatkhande).

- [Vishnu Narayan Bhatkhande](https://en.wikipedia.org/wiki/Vishnu_Narayan_Bhatkhande), Wikipedia.
- V. N. Bhatkhande, *Kramik Pustak Mālikā* (Hathras: Sangeet Karyalaya, reprinted).

## Carnatic swaras

Karnatak (South Indian) notation writes the svaras s r g m p d n by their initials, as
Hindustani notation does. Each rāga fixes which of the twelve svarasthānas each letter stands
for, so a letter's pitch depends on the rāga. Dots above and below mark the octave, and commas or
lengthened letters give the duration in the tāḷam's beats, which are divided by bars.

Svara letters are as old as the treatises. The printed notation took its present form with
Subbarama Dikshitar's *Saṅgīta Sampradāya Pradarśini* (1904), which printed the compositions of
Muthuswami Dikshitar and others with signs for the gamakas. Teachers' books and the music
colleges of the twentieth century spread it.

- [Carnatic music](https://en.wikipedia.org/wiki/Carnatic_music) and
  [Svara](https://en.wikipedia.org/wiki/Svara), Wikipedia.
- Subbarama Dikshitar, *Saṅgīta Sampradāya Pradarśini* (Ettayapuram, 1904); English translation
  online at [ibiblio.org/guruguha](https://www.ibiblio.org/guruguha/ssp.htm).

## Jianpu (numbered notation)

Jianpu (简谱, "simple notation") writes the degrees of the scale as the numbers 1 to 7, with 0 for a
rest. Dots above or below a number raise or lower it an octave, and lines under it halve its
length. Most Chinese songbooks and much instrumental music are written in it.

Jean-Jacques Rousseau proposed numbers for the degrees in 1742. Pierre Galin, Aimé Paris and
Émile Chevé made it a teaching method in France in the nineteenth century, the Galin-Paris-Chevé
system. It came to China by way of Japan around 1900, in the school songs (*xuetang yuege*) of Shen
Xingong and others, and became the common notation of the twentieth century. Indonesia uses a
version called *not angka*.

- [Numbered musical notation](https://en.wikipedia.org/wiki/Numbered_musical_notation), Wikipedia.
- Frederick Lau, *Music in China* (Oxford, 2008).

## Kepatihan (gamelan)

Kepatihan is the cipher notation of the Javanese gamelan. It writes the keys of a gamelan in
numbers: 1 2 3 5 6 for sléndro, and 1 to 7 for pélog. It is written in gatra of four beats, with
dots for the octaves and signs for the gong, kenong, kempul and kethuk. It usually notates the
balungan, the skeleton melody; the elaborating parts are left to the players.

It is named after the Kepatihan, the residence of the chief minister in Surakarta, where it was
devised around 1900, on the model of the European cipher notation. It replaced the
court's older graphic notations (*titilaras andha* and *titilaras ranté*). Through the
conservatories it became the gamelan's standard. See [Central Java](research:Central%20Java).

- [Kepatihan notation](https://en.wikipedia.org/wiki/Kepatihan), Wikipedia.
- Judith Becker, *Traditional Music in Modern Java* (University Press of Hawaii, 1980).
- Neil Sorrell, *A Guide to the Gamelan* (Faber, 1990).

## Jeongganbo (Korean)

Jeongganbo (정간보) is the grid notation of Korean court music. Each square (*jeonggan*) is one beat.
The pitch names (hwang, tae, jung, im, nam...) are written as Chinese characters in the squares,
and the squares are read top to bottom in columns from right to left. It is one of the first
notations in East Asia to give durations exactly.

It was created under King Sejong (r. 1418–1450), and the music of his reign is written in it in the
*Sejong sillok* (Veritable Records of King Sejong). Later books such as the *Sejo sillok* and the
*Daeak hubo* (1759) kept it, and the National Gugak Center still publishes court and literati
music in it.

- [Traditional music of Korea](https://en.wikipedia.org/wiki/Traditional_music_of_Korea), Wikipedia.
- Keith Howard, *Korean Music: A Listening Guide* (Seoul: National Center for Korean
  Traditional Performing Arts, 2006).

## Arabic maqām staff

Arab musicians write the maqāms on the Western staff with two signs added: the half-flat (a flat
with a stroke) and the half-sharp. The neutral seconds and thirds that these signs show are about
a quarter tone from the Western steps, and players tune them by ear for each maqām. The
twenty-four quarter tones are a theoretical frame, not a tuning that musicians play.

Mīkhāʾīl Mushāqa set out the 24-tone octave in his *Risāla al-Shihābiyya* (c. 1840). The Cairo
Congress of Arab Music (1932) took it as the reference and adopted the staff with its added signs
for teaching and printing. See [Arabic maqām](research:Arabic%20maq%C4%81m) and
[Mīkhāʾīl Mushāqa](research:M%C4%ABkh%C4%81%CA%BE%C4%ABl%20Mush%C4%81qa).

- [Arabic maqam](https://en.wikipedia.org/wiki/Arabic_maqam) and
  [Cairo Congress of Arab Music](https://en.wikipedia.org/wiki/Cairo_Congress_of_Arab_Music),
  Wikipedia.
- [Maqam World](https://www.maqamworld.com), with every maqām on the staff.

## Turkish makam staff (AEU)

Turkish makam music is written on the staff in the Arel-Ezgi-Uzdilek (AEU) system. It divides the
whole tone into nine Holdrian commas, and its four sharps and four flats show steps of one, four,
five and eight commas. Notes in this system are read as degrees of a makam, and a performer plays
some of them higher or lower than they are written.

Hüseyin Saadettin Arel, Suphi Ezgi and Salih Murat Uzdilek worked it out in the 1920s to 1940s.
The conservatories and Turkish radio took it up, and almost all Turkish makam music has been
printed in it since then. Others have since proposed more accurate systems. See
[Arel, Ezgi and Uzdilek](research:Arel,%20Ezgi%20and%20Uzdilek).

- [Turkish makam](https://en.wikipedia.org/wiki/Turkish_makam), Wikipedia.
- Karl L. Signell, *Makam: Modal Practice in Turkish Art Music* (Asian Music Publications, 1977).

## Persian dastgāh staff (koron and sori)

Persian music is written on the staff with two signs added. The koron, a flat drawn as a
triangle, lowers a note by about a quarter tone, and the sori, a sharp with one stroke, raises it.
The intervals they show are not exact quarter tones: musicians tune them by ear and by school.

Colonel Ali-Naqi Vaziri (1887–1979) brought them in after studying in Europe. He set them out in his tār
method (*Dastur-e tār*) and taught them in the music school he founded in Tehran
in 1923. The radif was written down in his notation from then on. See
[Iran (dastgāh)](research:Iran%20%28dastg%C4%81h%29).

- [Koron (music)](https://en.wikipedia.org/wiki/Koron_%28music%29),
  [Sori (music)](https://en.wikipedia.org/wiki/Sori_%28music%29) and
  [Ali-Naqi Vaziri](https://en.wikipedia.org/wiki/Ali-Naqi_Vaziri), Wikipedia.
- Hormoz Farhat, *The Dastgāh Concept in Persian Music* (Cambridge, 1990).

## Degree numbers

Writing the degrees of a scale as numbers names each note by its place in the mode and not by its
pitch. So the same numbers fit any key and any tuning. It is the idea behind jianpu, kepatihan and
the sargam. Roman numerals for the chords on each degree (I, IV, V) were set out by Gottfried
Weber (1817) and carried on in harmony teaching. The Nashville Number System, which Neal Matthews
Jr of the Jordanaires devised in the late 1950s, writes a song's chords as degree numbers so that
the session players can play it in any key.

- [Degree (music)](https://en.wikipedia.org/wiki/Degree_%28music%29) and
  [Nashville Number System](https://en.wikipedia.org/wiki/Nashville_Number_System), Wikipedia.
- Chas Williams, *The Nashville Number System* (self-published, 1988; 7th ed. 2005).

# Composition / Architectonics

## Architectonics

Architectonics is the architecture of a piece: how it is built from sections, phrases and
motives, how long each part lasts, and where its energy builds and falls. The word comes from
Kant's "architectonic of pure reason". Theorists use it for the formal design that holds a piece
together.

Boris Asafiev described form as a process (*Musical Form as a Process*, 1930–47), and Ernst Kurth
described musical energy. William Caplin (*Classical Form*, 1998) named the formal functions of
phrases. Fred Lerdahl and Ray Jackendoff (*A Generative Theory of Tonal Music*, 1983) described
grouping and reduction, and James Hepokoski and Warren Darcy set out sonata theory (2006). For
proportion, the golden section and the Fibonacci numbers have been read in Bartók (Ernő Lendvai,
1971) and Debussy (Roy Howat, 1983). Xenakis's *Metastaseis* (1954) took its proportions from the
golden-ratio Modulor of Le Corbusier. See [The golden ratio in music](research:The%20golden%20ratio%20in%20music)
and [Fibonacci numbers](research:Fibonacci%20numbers).

- [Musical form](https://en.wikipedia.org/wiki/Musical_form), Wikipedia.
- Ernő Lendvai, *Béla Bartók: An Analysis of His Music* (Kahn & Averill, 1971).
- Roy Howat, *Debussy in Proportion* (Cambridge, 1983).
- Fred Lerdahl and Ray Jackendoff, *A Generative Theory of Tonal Music* (MIT Press, 1983).
- William E. Caplin, *Classical Form* (Oxford, 1998).

In PHI: [Compose > Architectonics > Design a Set...](phi:architectonics.design),
[Compose a Song...](phi:architectonics.compose) and [Compose a Score...](phi:architectonics.score)

## Generative and endless music

Generative music is made by a system that keeps producing it, so that it is different each time
and need not end. Brian Eno named it in 1995–96. His *Generative Music 1* (1996) was made with
SSEYO's Koan software and played differently on each computer. Earlier examples are Terry Riley's
*In C* (1964), Eno's tape loops of different lengths on *Music for Airports* (1978), and the
algorithmic systems of Laurie Spiegel's *Music Mouse* (1986). Eno and Peter Chilvers' apps
*Bloom* (2008) and *Reflection* (2017) carried the idea onto phones.

- [Generative music](https://en.wikipedia.org/wiki/Generative_music), Wikipedia.
- Brian Eno, "Generative Music" (talk, San Francisco, 1996), in *In Motion Magazine*
  ([inmotionmagazine.com](https://inmotionmagazine.com/eno1.html)).
- Brian Eno, *A Year with Swollen Appendices* (Faber, 1996).

In PHI: [Compose > Architectonics > Design a Set...](phi:architectonics.design), then Endless:
a New Set at the End

# Composition / Europe

## Organum

Organum is the first written European polyphony: one or more voices added to a plainchant. In the
earliest form, *parallel organum*, the added voice moved with the chant a fourth or fifth below.
In the twelfth century the chant was held in long notes (the tenor, from *tenere*, to hold) while a
voice above it sang long melismas (*organum purum*). Where the chant had many notes, the voices
moved together in the rhythmic modes in *discant* sections, or *clausulae*.

The treatises *Musica enchiriadis* and *Scolica enchiriadis* (c. 900) describe parallel organum.
At Notre-Dame in Paris, Léonin (fl. 1150s–1201) wrote two-voice organa for the year's feasts, the
*Magnus liber organi*. Pérotin (fl. c. 1200) wrote them for three and four voices: *Viderunt
omnes* and *Sederunt principes*, sung at Christmas 1198 and St Stephen's day 1199. An English
student in Paris, known as Anonymous IV, named the two composers about 1280. See
[parallel organum](research:parallel%20organum) and [organum purum](research:organum%20purum).

- [Organum](https://en.wikipedia.org/wiki/Organum), [Léonin](https://en.wikipedia.org/wiki/L%C3%A9onin)
  and [Pérotin](https://en.wikipedia.org/wiki/P%C3%A9rotin), Wikipedia.
- Edward H. Roesner (ed.), *Le Magnus liber organi de Notre-Dame de Paris* (Éditions de
  l'Oiseau-Lyre, 1993–2009).
- Craig Wright, *Music and Ceremony at Notre Dame of Paris, 500–1550* (Cambridge, 1989).
- Johannes de Garlandia, *De mensurabili musica* (c. 1240), on the rhythmic modes.

In PHI: [Compose > Europe > Organum...](phi:compose.organum)

## Isorhythm

Isorhythm repeats a rhythmic pattern (the *talea*) and a melodic one (the *color*) in a voice of
a motet, usually the tenor. When the two have different lengths they meet again only after
several statements. The later motets also repeat the upper voices' rhythms (*panisorhythm*),
shorten the talea in proportion in the last sections (diminution), and pass the notes between
voices at the joins (*hocket*).

It grew out of the repeated rhythmic modes of the thirteenth-century motet. Philippe de Vitry
(1291–1361) made it a principle in the motets of the *Roman de Fauvel* (c. 1317) and in the *Ars
nova* treatise of about 1322 ascribed to him. Guillaume de Machaut (c. 1300–1377) used it in his
motets and in the *Messe de Nostre Dame*, and Johannes Ciconia and Guillaume Du Fay carried it into
the fifteenth century. Du Fay's *Nuper rosarum flores* (1436) was written for the consecration of
Florence Cathedral's dome. The word itself is modern: Friedrich Ludwig coined it in 1904.

- [Isorhythm](https://en.wikipedia.org/wiki/Isorhythm) and
  [Ars nova](https://en.wikipedia.org/wiki/Ars_nova), Wikipedia.
- Margaret Bent, "Isorhythm", *Grove Music Online*.
- Anne Walters Robertson, *Guillaume de Machaut and Reims* (Cambridge, 2002).

In PHI: [Compose > Europe > Isorhythm...](phi:compose.isorhythm)

## Species counterpoint

Species counterpoint teaches part-writing in five steps, the *species*, each against a given
melody, the *cantus firmus*. The first species is note against note, the second two notes
against one, and the third four against one. The fourth uses syncopations and suspensions, and the
fifth, florid counterpoint, mixes all of them. Each species has rules for consonance, dissonance,
motion and cadence.

Teachers had used steps like these since the sixteenth century (Zarlino, 1558; Diruta, 1609).
Johann Joseph Fux (1660–1741), Kapellmeister at the court of Vienna, set them out in *Gradus ad
Parnassum* (1725), a Latin dialogue between the master Aloysius, who stands for Palestrina, and
his pupil Josephus. Haydn studied it and taught Beethoven from it, Mozart used it with his pupils, and it is still
taught. Knud Jeppesen's *Counterpoint* (1931) brought it closer to Palestrina's own practice. See
[modal counterpoint](research:modal%20counterpoint).

- [Counterpoint](https://en.wikipedia.org/wiki/Counterpoint) and
  [Gradus ad Parnassum](https://en.wikipedia.org/wiki/Gradus_ad_Parnassum), Wikipedia.
- Johann Joseph Fux, *The Study of Counterpoint*, trans. Alfred Mann (Norton, 1965).
- Knud Jeppesen, *Counterpoint: The Polyphonic Vocal Style of the Sixteenth Century* (1931;
  Dover, 1992).

In PHI: [Compose > Europe > Species Counterpoint...](phi:compose.counterpoint)

## Fugue

A fugue is written in imitation on a short theme, the subject. Each voice enters with it in turn,
alternating between the subject in the tonic and the answer in the dominant (the exposition).
Episodes follow, often in sequence, and lead to middle entries in other keys. In the stretto the
entries overlap, and many fugues close over a pedal on the tonic.

*Fuga* ("flight") meant canon in the fourteenth century. The fugue grew from the imitative
ricercar and canzona of the sixteenth century, through Sweelinck, Frescobaldi and Froberger, and
north German organists such as Buxtehude. Johann Sebastian Bach brought it to its height in *The
Well-Tempered Clavier* (1722 and 1742), the organ fugues and *The Art of Fugue* (c. 1742–1750).
The "school fugue" of the Paris Conservatoire, codified by André Gedalge (*Traité de la fugue*,
1901), made it a set form for teaching.

- [Fugue](https://en.wikipedia.org/wiki/Fugue), Wikipedia.
- André Gedalge, *Treatise on the Fugue*, trans. A. Levin (Oklahoma, 1965).
- Alfred Mann, *The Study of Fugue* (Rutgers, 1958; Dover, 1987).
- Joseph Kerman, *The Art of Fugue: Bach Fugues for Keyboard, 1715–1750* (California, 2005).

In PHI: [Compose > Europe > Fugue...](phi:compose.fugue)

## Twelve-tone rows

A twelve-tone row orders the twelve pitch classes so that none is stressed over another. A
piece is made from the row's 48 forms: the prime, its inversion, its retrograde and its
retrograde inversion, each on all twelve transpositions, which the *matrix* sets out in a square.

Josef Matthias Hauer worked with "tropes" of the twelve notes from 1919. Arnold Schoenberg
(1874–1951) came to his method after his free atonal works and used it throughout in the Suite for
Piano, Op. 25 (1921–23). Alban Berg and Anton Webern took it up. Milton Babbitt (1916–2011) made
it a system: *combinatoriality*, in which halves of a row and of its transformations make
twelve-tone aggregates together, and the *time-point system*, which orders rhythm by the row too.
Boulez, Stockhausen and others carried it into the total serialism of the 1950s. See
[free atonality and twelve-tone harmony](research:free%20atonality%20and%20twelve-tone%20harmony).

- [Twelve-tone technique](https://en.wikipedia.org/wiki/Twelve-tone_technique), Wikipedia.
- Arnold Schoenberg, "Composition with Twelve Tones" (1941), in *Style and Idea* (1950).
- Milton Babbitt, "Twelve-Tone Invariants as Compositional Determinants", *Musical Quarterly*
  46/2 (1960).
- Andrew Mead, *An Introduction to the Music of Milton Babbitt* (Princeton, 1994).

In PHI: [Compose > Europe > 12-Tone Row...](phi:compose.serial)

## Stochastic music and sieves

Stochastic music is Iannis Xenakis's name for music composed with the laws of probability: masses
of sound (clouds of pizzicati, glissandi, grains) whose overall shape is chosen and whose single
events follow a distribution, as gas molecules follow the kinetic theory. Sieves are his later
tool for building scales and rhythms from residue classes, numbers that leave the same remainder.

Xenakis (1922–2001), an engineer and architect who worked with Le Corbusier, reacted against the
serialism of the 1950s, whose density he said the ear heard only as statistics. *Metastaseis*
(1953–54) and *Pithoprakta* (1955–56) were the first stochastic works; *Achorripsis* (1956–57)
used Poisson's law throughout, and from 1962 the ST pieces were calculated on an IBM 7090. He set
out the theory in *Musiques formelles* (1963), translated as *Formalized Music* (1971); sieves
came in *Nomos Alpha* (1965–66) and his article "Sieves" (1990). He founded EMAMu (1966, later CEMAMu) and built
the UPIC drawing system (1977) and the GENDY programs of dynamic stochastic synthesis (1991).

- [Iannis Xenakis](https://en.wikipedia.org/wiki/Iannis_Xenakis), Wikipedia.
- [Les Amis de Xenakis](https://www.iannis-xenakis.org), the composer's archive and catalogue.
- Iannis Xenakis, *Formalized Music: Thought and Mathematics in Composition* (Pendragon,
  revised edition 1992), and "Sieves", *Perspectives of New Music* 28/1 (1990).
- Thomas DeLio, "Iannis Xenakis' *Nomos Alpha*: The Dialectics of Structure and Materials",
  *Journal of Music Theory* 24/1 (1980).
- Nouritza Matossian, *Xenakis* (Kahn & Averill, 1986).
- Linda M. Arsenault, "Iannis Xenakis's *Achorripsis*: The Matrix Game", *Computer Music
  Journal* 26/1 (2002).

In PHI: [Compose > Europe > Stochastic Matrix...](phi:compose.stochastic) and
[Stochasticize the Clip...](phi:compose.stochasticize)

## Xenakis's cubes and Messiaen's techniques

In *Nomos Alpha* for solo cello (1965–66), written for Siegfried Palm, Xenakis ordered his
material by group theory. The 24 rotations of a cube permute its eight corners, and each corner
holds a sound (a pitch set, a density, a dynamic, a way of playing). The piece moves from one
rotation to the next in chains like Fibonacci's, so the order of the sounds turns as the cube
does. Sieves give its pitches and rhythms.

Xenakis studied with Olivier Messiaen in Paris from 1951, and Messiaen's own systems lie behind
it. The *modes of limited transposition*, set out in *Technique de mon langage musical* (1944),
repeat within the octave and so have only a few transpositions. *Non-retrogradable rhythms* are
palindromes. In *Mode de valeurs et d'intensités* (1949) each pitch keeps its own duration,
dynamic and attack, a step towards the total serialism of Boulez and Stockhausen. See
[Olivier Messiaen](research:Olivier%20Messiaen) and
[Stochastic music and sieves](research:Stochastic%20music%20and%20sieves).

- [Nomos Alpha](https://en.wikipedia.org/wiki/Nomos_Alpha) and
  [Mode of limited transposition](https://en.wikipedia.org/wiki/Mode_of_limited_transposition),
  Wikipedia.
- Iannis Xenakis, *Formalized Music* (Pendragon, 1992), ch. VIII, "Towards a Philosophy of
  Music".
- Moreno Andreatta, Carlos Agon and others, "Formal Aspects of Iannis Xenakis' 'Symbolic Music'",
  *Journal of New Music Research* 33/2 (2004).
- Olivier Messiaen, *The Technique of My Musical Language*, trans. John Satterfield (Leduc, 1956).

In PHI: [Compose > Europe > Cubify...](phi:compose.cubify)

## Chance and aleatory

Chance music leaves some of a piece to chance, in the writing or in the playing. John Cage cast
his music by chance operations: he tossed coins to consult the *I Ching*, the Chinese book of
changes, whose 64 hexagrams he took as the rows of charts of sounds, durations and dynamics.
Witold Lutosławski kept the pitches but let each player play a figure at their own speed,
unconducted, until a cue, which he called *aleatory counterpoint*.

Cage (1912–1992) wrote *Music of Changes* for David Tudor in 1951 with the *I Ching*, which
Christian Wolff had given him. Its form, like that of his works since the 1930s, was a square-root
structure, in which the large sections had the proportions of the phrases. Werner Meyer-Eppler
coined "aleatoric" in 1955, and Pierre Boulez's essay "Alea" (1957) and Stockhausen's
*Klavierstück XI* (1956) brought open form to the European avant-garde. Lutosławski (1913–1994)
heard Cage's Concert for Piano and Orchestra on the radio in 1960 and answered it with *Jeux
vénitiens* (1960–61). See [John Cage](research:John%20Cage).

- [Aleatoric music](https://en.wikipedia.org/wiki/Aleatoric_music) and
  [Music of Changes](https://en.wikipedia.org/wiki/Music_of_Changes), Wikipedia.
- James Pritchett, *The Music of John Cage* (Cambridge, 1993).
- Charles Bodman Rae, *The Music of Lutosławski* (Faber, 1994; 3rd ed. Omnibus, 1999).
- John Cage, *Silence: Lectures and Writings* (Wesleyan, 1961).

In PHI: [Compose > Europe > Chance and Aleatory...](phi:compose.aleatory)

## 4′33″

*4′33″* is John Cage's piece in three movements in which the performer plays nothing. Each
movement is marked *tacet*. At the first performance David Tudor closed the piano's lid at the
start of each movement and opened it at the end, and the movements lasted 33″, 2′40″ and 1′20″.
What is heard is whatever sounds in the room and outside it.

Tudor played it first at the Maverick Concert Hall in Woodstock, New York, on 29 August 1952.
Cage had heard two sounds in an anechoic chamber at Harvard in 1951, a high one and a low one,
which the engineer told him were his nervous system and his blood circulating, and concluded that
there is no such thing as silence. Robert Rauschenberg's all-white paintings (1951) gave him the
courage to write it. Cage called it his most important piece.

- [4′33″](https://en.wikipedia.org/wiki/4%E2%80%B233%E2%80%B3), Wikipedia.
- Kyle Gann, *No Such Thing as Silence: John Cage's 4′33″* (Yale, 2010).
- Douglas Kahn, *Noise, Water, Meat: A History of Sound in the Arts* (MIT Press, 1999).
- John Cage, *Silence* (Wesleyan, 1961).

In PHI: [Compose > Europe > 4′33″ (Silence)...](phi:compose.silence)

## Atlas Eclipticalis (star charts)

*Atlas Eclipticalis* (1961–62) is John Cage's work for any number of the 86 orchestral parts he
wrote for it. He laid transparent paper over the pages of Antonín Bečvář's star atlas *Atlas
Eclipticalis* (1958) and copied the stars as notes: across the page is time, up and down is pitch,
and a star's size gives its loudness. The players read their parts against a conductor whose arms
move like the hands of a clock. It may be played with *Winter Music*, and contact microphones
amplify the instruments.

Part of it was first played in Montreal in 1961. When Leonard Bernstein conducted it with the New
York Philharmonic in February 1964, some players sabotaged the electronics and the audience
walked out. Cage returned to star charts in *Etudes Australes* (1974–75) for piano and *Freeman
Etudes* (1977–90) for violin.

- [Atlas Eclipticalis](https://en.wikipedia.org/wiki/Atlas_Eclipticalis), Wikipedia.
- James Pritchett, *The Music of John Cage* (Cambridge, 1993).
- David Nicholls (ed.), *The Cambridge Companion to John Cage* (Cambridge, 2002).

In PHI: [Compose > Europe > Star Charts...](phi:compose.starcharts)

## Musicircus

*Musicircus* is John Cage's invitation for many musicians to play at once in one place, each
their own music, with nothing coordinated. The score is no more than the idea. The first was in
the Stock Pavilion of the University of Illinois at Champaign-Urbana on 17 November 1967, with
pianists, choirs, rock bands and films. Cage and Lejaren Hiller's *HPSCHD* (1969), for up to
seven harpsichords and 51 computer-generated tapes, was a Musicircus of the same kind. Cage put on
Musicircuses until his last years, and many have been put on since in his memory.

- [Musicircus](https://en.wikipedia.org/wiki/Musicircus), Wikipedia.
- James Pritchett, *The Music of John Cage* (Cambridge, 1993).
- Richard Kostelanetz (ed.), *Conversing with Cage* (Limelight, 1988).

In PHI: [Compose > Europe > Musicircus...](phi:compose.musicircus)

## Time brackets (the Number Pieces)

In his last years John Cage wrote some forty *Number Pieces*, each named for its number of players,
with a superscript for the order among works of that number: *Four*, *Five*, *Seven*, *One¹¹*,
*Seventy-Four*. Each part is a row of *time brackets*. Each bracket holds one sound or a few, with a
time window to begin it in and an overlapping one to end it in, such as 0′00″–0′45″ and
0′30″–1′15″. The sounds are few and quiet, cast by chance from a small gamut, and some brackets
are empty. The players follow stopwatches and do not listen for each other.

The first was *Two* (1987), and he wrote them until his death in 1992. The brackets give each
player a freedom within limits, a kind of attention that Cage called "anarchic harmony".

- [Number Pieces](https://en.wikipedia.org/wiki/Number_Pieces), Wikipedia.
- Rob Haskins, *John Cage* (Reaktion, 2012).
- James Pritchett, *The Music of John Cage* (Cambridge, 1993), on the last works.
- Benedict Weisser, on the Number Pieces, as the Compose method cites him.

In PHI: [Compose > Europe > Time Brackets...](phi:compose.timebrackets)

## Process music

Process music makes the gradual process of the piece the thing heard. In phasing, two players
start a pattern together and one gradually moves ahead, so the pattern drifts against itself. In
build-up, notes are added to a rest-filled pattern one at a time. Additive and subtractive process
lengthens or shortens a figure by a note at each repeat. Each stage is repeated until the next.

La Monte Young's long tones (*Trio for Strings*, 1958) and Terry Riley's *In C* (1964) began
minimalism. Steve Reich (b. 1936) found phasing with tape loops in *It's Gonna Rain* (1965), took it
to players in *Piano Phase* (1967) and set it out in his essay "Music as a Gradual Process" (1968).
*Drumming* (1970–71) used build-up, and *Music for 18 Musicians* (1974–76) a cycle of eleven
chords. Philip Glass (b. 1937), after working with Ravi Shankar in Paris in 1965–66, built his additive
process in *Music in Fifths* (1969), *Music in Twelve Parts* (1971–74) and *Einstein on the Beach*
(1976). Michael Nyman first applied "minimal" to music in 1968. See
[minimalist harmony](research:minimalist%20harmony).

- [Minimal music](https://en.wikipedia.org/wiki/Minimal_music) and
  [Phase music](https://en.wikipedia.org/wiki/Phase_music), Wikipedia.
- Steve Reich, *Writings on Music, 1965–2000* (Oxford, 2002).
- Keith Potter, *Four Musical Minimalists* (Cambridge, 2000).
- Michael Nyman, *Experimental Music: Cage and Beyond* (1974; Cambridge, 2nd ed. 1999).

In PHI: [Compose > Europe > Process Music...](phi:compose.process) and
[Minimalize a Scene...](phi:compose.minimalize)

## Tintinnabuli

Tintinnabuli ("little bells") is Arvo Pärt's two-voice technique. One voice, the melodic or
M-voice, moves mostly by step around a central note. The other, the T-voice, sings only the notes
of the tonic triad, each time the triad note nearest the melody above or below it. The two sound
as one, the melody and its resonance. Pärt likens them to the sinner and forgiveness.

Pärt (b. 1935) had written serial and collage works in Soviet Estonia. After a crisis he spent
years studying chant and early polyphony, and returned with the piano piece *Für Alina* (1976).
*Fratres*, *Cantus in Memory of Benjamin Britten* (a mensuration canon in which each voice is
slower than the one above it) and *Tabula rasa* came in 1977, and *Spiegel im Spiegel* in 1978.
He left the Soviet Union in 1980 and settled in Berlin, and ECM's recording *Tabula rasa* (1984)
made his music known.

- [Tintinnabuli](https://en.wikipedia.org/wiki/Tintinnabuli) and
  [Arvo Pärt](https://en.wikipedia.org/wiki/Arvo_P%C3%A4rt), Wikipedia.
- [Arvo Pärt Centre](https://www.arvopart.ee/en/).
- Paul Hillier, *Arvo Pärt* (Oxford, 1997).
- Andrew Shenton (ed.), *The Cambridge Companion to Arvo Pärt* (Cambridge, 2012).

In PHI: [Compose > Europe > Tintinnabuli...](phi:compose.tintinnabuli)

## Spectral music

Spectral music takes its harmony and form from the spectrum of sound: the partials of a harmonic
series, the spectra of instruments analysed by computer, and how they change in time. The
orchestra plays the partials of a fundamental as chords, and a piece moves gradually between
harmonic spectra and inharmonic ones, between sound and noise.

Gérard Grisey (1946–1998) and Tristan Murail (b. 1947), with Michaël Levinas, Hugues Dufourt and
Roger Tessier, founded the ensemble L'Itinéraire in Paris in 1973. Grisey's *Partiels* (1975),
built on the spectrum of a trombone's low E, and his cycle *Les Espaces acoustiques* (1974–85)
were the first great works of it. Murail's *Gondwana* (1980) follows the spectrum of a bell
turning into that of a brass instrument. Dufourt named it "musique spectrale" in 1979. Kaija
Saariaho, Magnus Lindberg and Georg Friedrich Haas took it further. See
[spectral harmony](research:spectral%20harmony) and
[Gérard Grisey and Tristan Murail](research:G%C3%A9rard%20Grisey%20and%20Tristan%20Murail).

- [Spectral music](https://en.wikipedia.org/wiki/Spectral_music), Wikipedia.
- Joshua Fineberg (ed.), "Spectral Music: History and Techniques", *Contemporary Music Review*
  19/2 (2000), with his "Guide to the Basic Concepts and Techniques of Spectral Music".
- Gérard Grisey, *Écrits ou l'invention de la musique spectrale* (MF, 2008).

In PHI: [Compose > Europe > Spectral...](phi:compose.spectral)

## Tuning Meditation

*The Tuning Meditation* is a piece by Pauline Oliveros for any number of voices, given to the
audience as much as to singers. Each singer breathes and sings a tone of their own, then listens
for a tone sung by another and tunes to it, and then sings a new tone that no one else is singing,
taking turns between these freely, a breath at a time. It ends when the singing dies away.

Oliveros (1932–2016) had written meditations in words since her *Sonic Meditations* (1971–72),
made with a group of women at the University of California, San Diego. *The Tuning Meditation* is
one of her most performed works and has been sung by choirs and crowds of thousands. See
[Pauline Oliveros and Deep Listening](research:Pauline%20Oliveros%20and%20Deep%20Listening).

- [Pauline Oliveros](https://en.wikipedia.org/wiki/Pauline_Oliveros), Wikipedia.
- Pauline Oliveros, *Sonic Meditations* (Smith Publications, 1974) and *Deep Listening: A
  Composer's Sound Practice* (iUniverse, 2005).
- [Center for Deep Listening](https://www.deeplistening.rpi.edu) at Rensselaer.

In PHI: [Compose > Europe > Tuning Meditation...](phi:compose.tuningmeditation)

## Deep Listening

Deep Listening is Pauline Oliveros's practice of listening to everything, all the time, and of
listening to one's own listening. It joins the focused attention given to one sound with the open
attention given to the whole field. The music of the Deep Listening Band is improvised in places
of long resonance, where every sound is heard against the space.

In 1988 Oliveros, the trombonist Stuart Dempster and Panaiotis recorded in the Dan Harpole Cistern,
an empty water cistern at Fort Worden, Port Townsend, Washington, with a reverberation of 45
seconds. Their album *Deep Listening* (New Albion, 1989) gave the practice its name, and the
group, with David Gamper, became the Deep Listening Band. Oliveros taught it in retreats and
founded the Deep Listening Institute, now the Center for Deep Listening at Rensselaer
Polytechnic Institute. See [Pauline Oliveros and Deep Listening](research:Pauline%20Oliveros%20and%20Deep%20Listening).

- [Deep Listening](https://en.wikipedia.org/wiki/Deep_Listening), Wikipedia.
- Pauline Oliveros, *Deep Listening: A Composer's Sound Practice* (iUniverse, 2005).
- [Center for Deep Listening](https://www.deeplistening.rpi.edu).

In PHI: [Compose > Europe > Deep Listening...](phi:compose.deeplistening)

# Composition / South and West Asia

## Rāga development

A Hindustani performance unfolds a rāga in stages that grow in pulse and speed. In the *ālāp*,
with no pulse, the musician brings in the rāga note by note from the lower Sa, dwelling on its
*vādī* and *samvādī* and its characteristic phrases, and rising to the upper Sa (*baṛhat*). The
*joṛ* adds a pulse, and the *jhālā* quickens it with the drone strings (*chikārī*). Then the
*gat*, a composition in a tāl with the tablā, is varied and ends with a *tihāī*, a phrase played
three times to land on *sam*. The tānpūrā's drone sounds throughout.

The order of ālāp before the composition goes back to the *dhrupad* of the Mughal courts, and to
the *ālapti* of the old treatises. The sitār and sarod players of the nineteenth and twentieth
centuries, from the Seni gharānā to Allauddin Khan and his pupils Ravi Shankar and Ali Akbar Khan,
took it into instrumental music. See [Hindustani (North India)](research:Hindustani%20%28North%20India%29),
[Rāga](research:R%C4%81ga) and [North Indian tāl](research:North%20Indian%20t%C4%81l).

- [Raga](https://en.wikipedia.org/wiki/Raga) and [Alap](https://en.wikipedia.org/wiki/Alap),
  Wikipedia.
- Nazir A. Jairazbhoy, *The Rāgs of North Indian Music* (Faber, 1971).
- Joep Bor (ed.), *The Raga Guide* (Nimbus, 1999).
- Martin Clayton, *Time in Indian Music* (Oxford, 2000).

In PHI: [Compose > South and West Asia > Rāga Development...](phi:compose.raga)

## Taqsīm and waṣla

A *taqsīm* is an unmetred improvisation that lays out a maqām. It moves through the maqām's
*ajnās* (tetrachords and pentachords) from the tonic (*qarār*) up to the dominant (*ghammāz*) and
the octave (*jawāb*), modulates to related maqāms and comes home with a cadence (*qafla*). A
*waṣla* is a suite in one maqām. It might hold an instrumental prelude, taqsīms, a *samāʿī* (an
instrumental piece in four sections, *khānāt*, each followed by the refrain, the *taslīm*),
*muwashshaḥāt* (strophic songs) and vocal improvisation.

The waṣla was the form of the urban music of Cairo, Aleppo and Damascus in the nineteenth and early
twentieth centuries. The samāʿī came from the Ottoman *saz semaisi*. Recordings, radio and the
long songs of Umm Kulthum and Muhammad Abd al-Wahhab changed the concert in the twentieth century,
and the waṣla was revived from the 1970s. See [Arabic maqām](research:Arabic%20maq%C4%81m).

- [Taqsim](https://en.wikipedia.org/wiki/Taqsim) and [Wasla](https://en.wikipedia.org/wiki/Wasla)
 , Wikipedia.
- Scott Marcus, *Music in Egypt* (Oxford, 2007).
- A. J. Racy, *Making Music in the Arab World: The Culture and Artistry of Ṭarab* (Cambridge,
  2003).
- Habib Hassan Touma, *The Music of the Arabs* (Amadeus, 1996).

In PHI: [Compose > South and West Asia > Maqām: Taqsīm and Waṣla...](phi:compose.maqam)

## Radif and dastgāh

The *radif* is the body of melodies of Persian classical music, handed down from teacher to
pupil. It is arranged in seven *dastgāhs* and five *āvāzes*. Each holds a series of short
melodies, the *gushe-hā*. A performance opens with the *darāmad*, and each later gushe centres on
a higher note (its *shāhed*) until the *owj*, the highest point, and the *forud* brings the music
back to the dastgāh's home. Rhythmic pieces end it: the *chahārmezrāb* and the *reng*, a dance.

The radif took its form at the Qajar court in the nineteenth century. The brothers Mirzā
ʿAbdollāh (1843–1918) and Mirzā Hoseyn-qoli (1853–1915), sons of the tār player ʿAli-Akbar
Farāhāni, taught the radif that most versions descend from. Ali-Naqi Vaziri wrote it down in the
1920s. Musā Maʿrufi's 1963 edition and the recordings of Nur-Ali Borumand and others carried it on.
The radif was inscribed on UNESCO's list of the intangible heritage in 2009. See
[Iran (dastgāh)](research:Iran%20%28dastg%C4%81h%29).

- [Radif (music)](https://en.wikipedia.org/wiki/Radif_%28music%29) and
  [Dastgah](https://en.wikipedia.org/wiki/Dastgah), Wikipedia.
- Bruno Nettl, *The Radif of Persian Music* (Elephant & Cat, 1987; rev. 1992).
- Jean During, Zia Mirabdolbaghi and Dariush Safvat, *The Art of Persian Music* (Mage, 1991).
- Hormoz Farhat, *The Dastgāh Concept in Persian Music* (Cambridge, 1990).

In PHI: [Compose > South and West Asia > Radif and Dastgāh...](phi:compose.radif)

## Tablā qāʿida

A *qāʿida* ("rule") is a tablā composition that is a theme for variation. It is played as a theme
of bols (the syllables of the strokes, such as dha, ge, na, tin), then as variations (*palṭā*)
built only from the theme's own cells and rearranged by rules. Each line is played *bharī*
("full", with the open bass of the bāyāṅ) and then *khālī* ("empty", the bass strokes stopped:
dha becomes ta, ge becomes ke), returning to open for *sam*. A *tihāī* closes it, over the
*lehrā*, a repeating melody on the sāraṅgī or harmonium.

The qāʿida is the heart of the Delhi and Lucknow gharānās' solo repertoire. Jim Kippen studied it
in Lucknow from the 1970s with Ustad Afaq Hussain Khan, and he and Bernard Bel modelled its rules
with grammars in the Bol Processor (next entry).

- [Tabla](https://en.wikipedia.org/wiki/Tabla), Wikipedia.
- James Kippen, *The Tabla of Lucknow: A Cultural Analysis of a Musical Tradition* (Cambridge,
  1988).
- Robert S. Gottlieb, *Solo Tabla Drumming of North India* (Motilal Banarsidass, 1993).

In PHI: [Compose > South and West Asia > Bol Processor Grammar...](phi:compose.bolgrammar)

## Bol Processor

The Bol Processor is a program that composes with formal grammars, the rewriting rules of
linguistics. A grammar's rules, weighted and in subgrammars, rewrite a start symbol step by step
into a string of sound-objects. Run in reverse, it checks whether a given piece follows the rules,
so a musician can test a model against what a teacher accepts.

Bernard Bel, a computer scientist, and the ethnomusicologist James Kippen began it in the early
1980s to model the tablā qāʿida. Kippen took the program to Lucknow, where tablā players judged the
variations it made, and the grammars were corrected from what they said. BP2, for the Macintosh
(1990s), added polymetric expressions, time-objects, MIDI and Csound output. Bel later released
BP3 as open source, with a web interface. See [Tablā qāʿida](research:Tabl%C4%81%20q%C4%81%CA%BFida).

- [Bol Processor](https://bolprocessor.org), the project's site, with its history and examples.
- [bolprocessor on GitHub](https://github.com/bolprocessor/bolprocessor), the BP3 source.
- James Kippen and Bernard Bel, "Modelling Music with Grammars: Formal Language Representation in
  the Bol Processor", in A. Marsden and A. Pople (eds.), *Computer Representations and Models in
  Music* (Academic Press, 1992).
- Bernard Bel, "Rationalizing Musical Time: Syntactic and Symbolic-Numeric Approaches", in
  C. Barlow (ed.), *The Ratio Book* (Feedback Studio, 2001).

In PHI: [Compose > South and West Asia > Bol Processor...](phi:compose.bolprocessor)

# Composition / East and Southeast Asia

## Gamelan colotomic form

Javanese gamelan pieces (*gendhing*) are built on gong cycles. The *balungan*, the skeleton
melody, moves in *gatra* of four beats, the last of which, the *sèlèh*, is the strongest. The
cycle's points are marked by the gongs and kettles: the great gong at the end, the kenong
dividing it, the kempul between them and the kethuk between those. This is the *colotomic*
structure. In a *ladrang* the cycle is 32 beats. The bonang and the saron peking elaborate the
balungan, and as the drummer slows the tempo into a higher *irama* level they play twice as many
notes to each beat. A *buka* (introduction) begins the piece and a *suwuk* ends it.

The gamelan of the courts of Surakarta and Yogyakarta took its classical form in the eighteenth
and nineteenth centuries. Jaap Kunst coined "colotomic" (*Music in Java*, 1934), and Mantle Hood,
Judith Becker and the Javanese theorist Martopangrawit described the irama levels and the
pathet. See [Central Java](research:Central%20Java), [gamelan layering](research:gamelan%20layering)
and [Martopangrawit](research:Martopangrawit).

- [Gamelan](https://en.wikipedia.org/wiki/Gamelan) and
  [Colotomy](https://en.wikipedia.org/wiki/Colotomy), Wikipedia.
- Judith Becker, *Traditional Music in Modern Java* (Hawaii, 1980).
- Sumarsam, *Gamelan: Cultural Interaction and Musical Development in Central Java* (Chicago,
  1995).
- Richard Pickvance, *A Gamelan Manual: A Player's Guide to the Central Javanese Gamelan*
  (Jaman Mas, 2005).

In PHI: [Compose > East and Southeast Asia > Gamelan Colotomic Form...](phi:compose.gamelan)

## Jo-ha-kyū

Jo-ha-kyū is the Japanese principle of a three-part movement: *jo*, a slow opening; *ha*,
"breaking", a development that quickens; and *kyū*, a rapid close. It shapes a whole day's program,
a piece and each phrase within it.

It came from gagaku, the court music that came from China and Korea and was settled in Japan by
the tenth century, whose suites move from a free prelude (*netori*) through slow pieces to fast
ones. Zeami Motokiyo (c. 1363–c. 1443), the master of nō, made it the law of all performance in
his treatises, above all *Kakyō* (1424). It is in the nō play's order of scenes, in kabuki and
bunraku, and in the tea ceremony and the martial arts. See [Japan](research:Japan) and
[shō clusters (aitake)](research:sh%C5%8D%20clusters%20%28aitake%29).

- [Jo-ha-kyū](https://en.wikipedia.org/wiki/Jo-ha-ky%C5%AB) and
  [Gagaku](https://en.wikipedia.org/wiki/Gagaku), Wikipedia.
- Zeami, *On the Art of the Nō Drama*, trans. J. Thomas Rimer and Yamazaki Masakazu (Princeton,
  1984).
- William P. Malm, *Traditional Japanese Music and Musical Instruments* (Kodansha, 2000).
- Robert Garfias, *Music of a Thousand Autumns: The Tōgaku Style of Japanese Court Music*
  (California, 1975).

In PHI: [Compose > East and Southeast Asia > Jo-ha-kyū...](phi:compose.johakyu)

## Sanjo

*Sanjo* ("scattered melodies") is a Korean form for a solo melodic instrument with the janggu
drum. It is played in movements on rhythmic cycles (*jangdan*) that go from slow to fast:
*jinyangjo*, *jungmori*, *jungjungmori*, *jajinmori* and *hwimori*. The melody builds tension by
bending and shaking its notes (*nonghyeon*) and releases it (*maeggo* and *pulgo*, "tying" and
"untying").

It grew in the 1890s from the shaman music (*sinawi*) and *pansori* of the southwest. Kim Chang-jo
(1865–1919) is said to have made the first sanjo, for the gayageum, and Baek Nak-jun made one for
the geomungo in 1896. Each master's version is a school (*-ryu*), and sanjo were later made for the
daegeum, haegeum, ajaeng and piri. Gayageum sanjo became a National Intangible Cultural Property in
1968. See [Korea](research:Korea), [Korean jangdan](research:Korean%20jangdan) and
[sinawi](research:sinawi).

- [Sanjo (music)](https://en.wikipedia.org/wiki/Sanjo_%28music%29), Wikipedia.
- Keith Howard, *Korean Music: A Listening Guide* (National Center for Korean Traditional
  Performing Arts, 2006).
- Lee Byong Won, *Styles and Esthetics in Korean Traditional Music* (National Center for Korean
  Traditional Performing Arts, 1997).
- Robert C. Provine, "Sanjo", *Grove Music Online*.

In PHI: [Compose > East and Southeast Asia > Sanjo...](phi:compose.sanjo)

## Sǎn-màn-zhōng-kuài-sǎn (banshi variation)

*Sǎn, màn, zhōng, kuài, sǎn* ("free, slow, moderate, fast, free") is the shape of many Chinese
instrumental pieces and suites. A tune is played in metrical forms (*banshi*), each faster and
denser than the last, with free passages at the start and end. The tunes (*qupai*) are often in
four phrases: *qǐ* (beginning), *chéng* (continuing), *zhuǎn* (turning) and *hé* (closing), as in
the poetic quatrain. Phrases may be linked by beginning on the note the last ended on, *yú yǎo wěi*
("the fish bites its tail").

The banshi system is as old as Chinese opera, whose arias change speed and metre by banshi. In
instrumental music it shapes the *sizhu* ("silk and bamboo") ensembles of Jiangnan, the pipa's
large pieces and the solo repertoires that the conservatories took up in the twentieth century.
See [China](research:China) and [sizhu heterophony](research:sizhu%20heterophony).

- [Jiangnan sizhu](https://en.wikipedia.org/wiki/Jiangnan_sizhu), Wikipedia.
- J. Lawrence Witzleben, *"Silk and Bamboo" Music in Shanghai* (Kent State, 1995).
- Frederick Lau, *Music in China* (Oxford, 2008).
- Stephen Jones, *Folk Music of China: Living Instrumental Traditions* (Oxford, 1995).

In PHI: [Compose > East and Southeast Asia > Sǎn-màn-zhōng-kuài-sǎn...](phi:compose.chinese)

# Composition / Africa

## Ewe layered ostinato

Ewe dance-drumming of Ghana and Togo is built in layers over a bell. The *gankogui* (double bell)
plays a timeline of twelve pulses, the standard pattern of West Africa, and the *axatse* (rattle)
plays with it. The supporting drums, *kaganu*, *kidi* and *sogo*, each repeat their own ostinato,
placed against the bell so that groupings of three and two sound together. The lead drum, such as
the *atsimevu*, calls, and the support drums answer. A song leader and chorus sing with it.
Agbekor, Gahu and Atsiagbekor are pieces of this kind.

A. M. Jones (*Studies in African Music*, 1959) first transcribed the parts, and David Locke,
with the master drummer Godwin Agbeli, and Kofi Agawu wrote the later studies. See
[West African timelines](research:West%20African%20timelines).

- [Ewe music](https://en.wikipedia.org/wiki/Ewe_music) and
  [Agbekor](https://en.wikipedia.org/wiki/Agbekor), Wikipedia.
- David Locke, *Drum Gahu* (White Cliffs, 1987), and "Africa/Ewe, Mande, Dagbamba, Shona,
  BaAka", in Jeff Todd Titon (ed.), *Worlds of Music*.
- Kofi Agawu, *Representing African Music* (Routledge, 2003).
- A. M. Jones, *Studies in African Music* (Oxford, 1959).

In PHI: [Compose > Africa > Layered Ostinato...](phi:compose.ewe)

## Hocket and interlocking

In a hocket, a melody is shared out among several players, each of whom plays only some of its
notes, so the melody is heard only in the whole. The horn ensembles of the Banda-Linda of the
Central African Republic play this way: each of a dozen or more horns, cut from roots or antelope
horn, has one pitch, and the figures interlock into a cyclic melody. Each player varies their
figure within the cycle while the melody stays.

Simha Arom recorded and analysed these ensembles from the 1960s, playing each part back to the
players to separate it, and published *African Polyphony and Polyrhythm* (1985, English 1991).
Gerhard Kubik described the "inherent patterns" that arise in such playing. The word *hocket* is
from medieval Europe, where the motets of the thirteenth and fourteenth centuries passed the
notes between voices in the same way. György Ligeti and Steve Reich took the technique from Arom's
recordings. See [horn hocket](research:horn%20hocket).

- [Hocket](https://en.wikipedia.org/wiki/Hocket), Wikipedia.
- Simha Arom, *African Polyphony and Polyrhythm: Musical Structure and Methodology* (Cambridge,
  1991).
- Gerhard Kubik, *Theory of African Music* (Wilhelmshaven, 1994; Chicago, 2010).

In PHI: [Compose > Africa > Hocket and Interlocking...](phi:compose.hocket)

## Congolese rumba and sebene

Congolese rumba is a guitar band music of Kinshasa and Brazzaville. Three guitars interlock on a
short chord cycle: the *accompagnement* repeats a rhythm-guitar figure, the *mi-solo* fills the
middle, and the *solo* plays short melodic cells above. A song begins as a slow sung rumba, the
voices in close thirds. Then comes the *sebene*, a long instrumental section in which the cycle
repeats over the drummer's driving *cavacha* rhythm and the solo guitar varies and climbs, while
an *atalaku* shouts to the dancers.

It grew from the Cuban son and rumba records sold in the Congo from the 1930s. Wendo Kolosoy's
"Marie-Louise" (1948) was an early hit. Joseph Kabasele's African Jazz ("Indépendance Cha Cha",
1960), with the guitarist Nico Kasanda, and Franco Luambo's TPOK Jazz (from 1956) made it the
dance music of Africa. The faster soukous of the 1970s and 1980s and the ndombolo came from it.
Congolese rumba was inscribed on UNESCO's list of the intangible heritage in 2021. See
[congolese rumba](research:congolese%20rumba).

- [Congolese rumba](https://en.wikipedia.org/wiki/Congolese_rumba) and
  [Soukous](https://en.wikipedia.org/wiki/Soukous), Wikipedia.
- Gary Stewart, *Rumba on the River: A History of the Popular Music of the Two Congos* (Verso,
  2000).
- Bob W. White, *Rumba Rules: The Politics of Dance Music in Mobutu's Zaire* (Duke, 2008).
- Banning Eyre, *Guitar Atlas: Africa* (Alfred, 2006).

In PHI: [Compose > Africa > Congolese Guitar: Rumba and Sebene...](phi:compose.congolese)

## Interlocking polyphony (Aka and Mbuti)

The Aka of the Central African Republic and Congo and the Mbuti of the Ituri forest in Congo sing
in many parts without a written score or a leader who conducts. Each voice sings its own short
figure, often yodelled between chest and head voice, over a common pulse, and the figures, of
different lengths, fit into one cycle. They overlap and fill each other's gaps, so the melody is
in the whole and no one sings it alone. Each singer varies their figure as they go, and the
whole stays.

Colin Turnbull recorded the Mbuti in 1957–58. Simha Arom studied the Aka from the 1970s, and
Susanne Fürniss and Michelle Kisliuk followed. UNESCO proclaimed the Aka's oral traditions a
Masterpiece of the Oral and Intangible Heritage in 2003. Their music reached Western composers,
among them Ligeti, and the jazz musician Herbie Hancock. See
[aka and baka polyphony](research:aka%20and%20baka%20polyphony) and
[Lift-up-over sounding](research:Lift-up-over%20sounding).

- [Music of the Central African Republic](https://en.wikipedia.org/wiki/Music_of_the_Central_African_Republic)
  and [Mbuti people](https://en.wikipedia.org/wiki/Mbuti_people), Wikipedia.
- Simha Arom, *African Polyphony and Polyrhythm* (Cambridge, 1991).
- Michelle Kisliuk, *Seize the Dance! BaAka Musical Life and the Ethnography of Performance*
  (Oxford, 1998).
- Colin Turnbull and Francis S. Chapman, *Mbuti Pygmies of the Ituri Rainforest* (Smithsonian
  Folkways, 1992).

In PHI: [Compose > Africa > Interlocking Polyphony...](phi:compose.interlocking)

# Composition / Rhythm

## Polymeter

In polymeter, patterns of different lengths repeat against each other on one pulse, so they meet
again only when the lengths' least common multiple comes round. A cycle of three against one of
four lines up every twelve beats. In polyrhythm, different divisions of one span sound together,
such as three against two in a bar.

Both are at the heart of the music of sub-Saharan Africa, where the bell's timeline and the
drums' ostinatos are set against one another. A. M. Jones and Simha Arom described them. The
medieval isorhythmic motet set a talea against a color of another length, and twentieth-century
composers such as Conlon Nancarrow, Steve Reich and György Ligeti built pieces from layered
cycles. In electronic and dance music, step sequencers with tracks of different lengths make
polymeter easy, as do the Euclidean rhythms. See [Euclidean rhythms](research:Euclidean%20rhythms)
and [Ewe layered ostinato](research:Ewe%20layered%20ostinato).

- [Polyrhythm](https://en.wikipedia.org/wiki/Polyrhythm) and
  [Metre (music): polymeter](https://en.wikipedia.org/wiki/Metre_%28music%29#Polymeter), Wikipedia.
- Godfried T. Toussaint, *The Geometry of Musical Rhythm* (CRC Press, 2nd ed. 2019).
- Simha Arom, *African Polyphony and Polyrhythm* (Cambridge, 1991).

In PHI: [Compose > Rhythm Generator...](phi:compose.rhythm)
