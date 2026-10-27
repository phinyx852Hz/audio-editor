# Rhythmic modes

The owner asked on 2026-10-26 for "a submenu for rhythmic modes, that would include all north
indian tala, south indian talam, all classic breakbeats, trap beats, medieval european rhythmic
modes, and of course, Caribbean, south american, and west and north african rhythms, and if
there's anything from East Asia, that would be great". Every name is lower case and in proper
transliteration (owner).

The menu is **Mode > rhythm** (owner, 2026-10-27: the Mode menu holds melody, tuning and rhythm). It has a submenu for each region, in alphabetical order: africa, east asia, europe, south asia, the americas and west asia. Europe is one region among the others (owner, 2026-10-27). Each region has a submenu for each tradition. Hovering over a
rhythm shows what it is and how it is counted: the ṭhekā's bols, the kriyās, or the strokes.
Picking a rhythm opens a dialog with its sounds, the number of cycles, and a tempo, which starts
at a usual tempo for that rhythm, and a laya (see below). The dialog has these buttons, and **edit in the generator...**:

- **add to the mixer** saves the rhythm as a MIDI file in the mix's Edits folder and puts it on the
  selected track at the playhead. If the track has no instrument, it gets the Synth, which plays
  General MIDI drums on channel 10. Once the rhythm is a MIDI clip, the clip's fast edits (Φ
  shuffle, duplicate, half and double density, reverse) work on it.
- **write a tracker pattern** adds a new pattern at the end of the Tracker's song, or starts a new
  song. Each sound gets its own channel and instrument. The instruments are named after their
  General MIDI drum and are silent until you load a sample into each one or link it to a Retro
  Sampler slot. The pattern sets its own speed and tempo in its first row (Fxx), so it plays at the
  rhythm's tempo in any song. The pattern uses as many rows per beat as the finest step needs: one
  for tīntāl's ṭhekā, four with tirakiṭa or 16ths, and twelve for trap's triplet hats.
- **save as midi...** writes a type 0 MIDI file.

Code: `Source/mixer/Rhythms.{h,cpp}` holds the library, the MIDI writer and the tracker grid, all
in plain C++. `Source/ui/RhythmSection.cpp` holds the menu, the dialog and the Tracker writer.
`MixerWindow::addRhythm` places the clip. The tests are in `testRhythms` in
`tests/CoreTests.cpp`. They check that every lane is exactly one cycle long, that every name is
lower case, that every rhythm round-trips through a MIDI file, and that the tāl and tāḷam
structures match the sources.

## How a rhythm is written

Each rhythm is a set of lanes. A lane is one sound, either a General MIDI drum or a pitched note,
with one character for each step:

| character | meaning |
|---|---|
| `X` | accent (velocity 120) |
| `x` | hit (96) |
| `o` | soft: a ghost note or a finger count (56) |
| `-` | the previous note held on |
| `.` | silent |

Each lane has its own number of steps per beat. That way the hats of a trap beat can be in
triplets while the kick stays in 16ths.

## Transliteration

| language | system | examples |
|---|---|---|
| Sanskrit, Hindi, Tamil and Telugu terms | IAST, as in the rāga sheets (PR #59) | tīntāl, ādi tāḷam |
| Arabic | IJMES | maqsūm, ṣaʿīdī |
| Korean | Revised Romanization | jungjungmori, gutgeori |
| Ewe | its own orthography | agbekɔ |
| Spanish and Portuguese | their own spelling | güiro, baião, cha-cha-chá |
| Latin | its own spelling | modus primus |

Hindustani names follow their Hindi pronunciation, without the final schwa (tīntāl, not tīntāla).
Karnatak names follow the Sanskrit-Tamil usage of the tradition (tāḷam, jāti, aṅga). English
names are in plain lower case, with the IPA in the tooltip (owner, 2026-10-27: both).

## South Asia: north indian tāl (16)

tīntāl, jhaptāl, ektāl, rūpak, dādrā, kaharvā, jhūmrā, dhamār, cautāl, āḍā cautāl, dīpcandī,
tilvāḍā, sūltāl, tīvrā, jat tāl, pañjābī (sitārkhānī).

Each tāl is built from its ṭhekā. The vibhāgs follow Bhatkhande's signs: x for sam, 0 for khālī,
and a number for each tālī. Rūpak's sam falls on its khālī.

The bols are sorted onto the tabla's strokes, each played by a General MIDI conga or bongo:

| stroke | bols | General MIDI sound |
|---|---|---|
| bāyāṃ, open | ge, ghe, and the bass of dhā and dhin | low conga |
| bāyāṃ, closed | ka, kat, ke, ki | low bongo |
| dāyāṃ, open | nā, tā, tin, tū, and the treble of dhā and dhin | open high conga |
| dāyāṃ, closed | ti, ra, ṭa, di | muted high conga |

Two more lanes carry the hand-keeping: a clap on sam and on each tālī, and a side stick on each
khālī (the wave).

The theoretical literature counts more than a hundred tāls. Rarer ones such as brahmatāl,
pañcam savārī, rudra, matta, śikhar and lakṣmī are not included yet, because their ṭhekās vary
between gharānās and books. They can be added once the owner names the versions they want.

## South Asia: south indian tāḷam (39)

The library has all 35 sūḷādi sapta tāḷas: dhruva, maṭhya, rūpaka, jhampa, tripuṭa, aṭa and ēka,
each in the five jātis:

| jāti | laghu length |
|---|---|
| tiśra | 3 |
| caturaśra | 4 |
| khaṇḍa | 5 |
| miśra | 7 |
| saṅkīrṇa | 9 |

A drutam is 2 akṣaras and an anudrutam is 1. Caturaśra jāti tripuṭa is listed as ādi tāḷam. The
jāti usually meant by a bare tāḷa name is marked in its description: dhruva, maṭhya, rūpaka and ēka
in caturaśra, jhampa in miśra, tripuṭa in tiśra, and aṭa in khaṇḍa.

The four cāpu tāḷams are also included: miśra (3+2+2), khaṇḍa (2+3), tiśra (1+2) and saṅkīrṇa
(4+5).

The kriyās are played as sounds:

| kriyā | General MIDI sound |
|---|---|
| clap (taṭṭu) | hand clap |
| a laghu's finger counts | side stick |
| a drutam's wave (vīccu) | high wood block |

A low conga marks each aṅga and accents samam. A muted conga keeps the gati: ta ka dhi mi, four
pulses to the akṣara in caturaśra naḍai. The other gatis (tiśra, khaṇḍa, miśra and saṅkīrṇa
naḍai) would take each tāḷam's 35 versions to 175 and are left for later.

## The Americas: breakbeats (10)

amen break, funky drummer, think (about it), apache, impeach the president, when the levee breaks,
funky president, cold sweat, it's a new day, the big beat.

Each one is the main bar (the amen break has four bars) as drum machines are usually programmed from
the record. They are simplified: the drummers vary every bar, and ghost notes are included only
where they define the groove. Each one names its record and year, and its drummer where that is
documented. These are our own transcriptions of rhythms, not samples.

## The Americas: trap (3)

| rhythm | what it has |
|---|---|
| trap (half-time) | the clap on the third beat, 808 kicks, 16th hats rolling into 32nds |
| trap (triplet hats) | the hats in 16th-note triplets |
| drill | swung triplet hats, and the snare on the third beat and late in the second bar |

All three are at 140 to 142 bpm.

## Europe: medieval rhythmic modes (6)

These are the six modi of the Notre-Dame school, as Johannes de Garlandia sets them out (c. 1240):

| mode | pattern | foot |
|---|---|---|
| modus primus | L B | trochaic |
| modus secundus | B L | iambic |
| modus tertius | L. B B(altera) | dactylic |
| modus quartus | B B(altera) L. | anapaestic |
| modus quintus | L. L. | spondaic |
| modus sextus | B B B | tribrachic |

They are in ternary time, with three tempora to a perfection. Each mode is played on a tabor and
sung on d (channel 1, held for its value), so the longs sound long.

## The Americas: caribbean (19)

son clave (3-2 and 2-3), rumba clave (3-2 and 2-3), cáscara (3-2 and 2-3, the timbal shell over its
clave, after Mauleón), tresillo, cinquillo, habanera, son montuno
(tumbao, 2-3), cha-cha-chá, bembé (6/8), one drop, rockers, steppers, ska, dembow, soca, merengue.

## The Americas: south america (8)

samba (batucada), bossa nova, baião, tango (3-3-2), milonga, candombe, cumbia, and huayno (the
Andes: a stressed beat followed by two short ones, written as an 8th and two 16ths in 2/4 on the
bombo).

Still to come: chacarera, marinera, maracatu, ijexá, partido alto, festejo and joropo. Haitian
konpa and Mexican son jarocho too. Each waits for a transcription that can be cited.

## Africa: west africa (6) and north africa (6)

West African:

- agbekɔ, with the standard 12/8 bell (gankogui), the axatse rattle and the kagan
- the fume-fume, gahu, shiko and soukous timelines
- afrobeat

North African:

- the Egyptian iqāʿāt maqsūm, baladī, ṣaʿīdī, malfūf and ayyūb, played as dum (low conga) and tak
  (open high conga)
- gnāwa, with the qarāqib and the gumbrī's beat

Not yet included: the Mande djembe rhythms (kuku, soli, dundunba). Their parts differ between
teachers.

## West Asia: iqāʿāt (1)

samāʿī thaqīl is the 10/8 of the samāʿī, grouped 3+2+2+3. Dum falls on eighths 1, 6 and 7, and
tak on 4 and 8.

Not included yet: çiftetelli, wahda w-nuṣṣ and the other long Arab and Ottoman cycles. They can be
added from Maqam World's iqāʿ pages once each one is checked.

## Laya and naḍai

The rhythm's dialog has a **laya (speed)** choice: as written, duguṇ (twice as fast), tiguṇ (three
times) or cauguṇ (four times). This is layakārī. Each sound plays its cycle that many times in the
same cycle, so tīntāl stays 16 mātrās with two, three or four bols to the mātrā. The lanes that keep
the cycle stay as they are: the tāl's claps and waves, and the tāḷam's kriyās. It works on every
rhythm, so a breakbeat at duguṇ is the break in double time.

A South Indian tāḷam also gets a **naḍai (gati)** choice: caturaśra (4 pulses to the akṣara),
tiśra (3), khaṇḍa (5), miśra (7) or saṅkīrṇa (9). Its pulse lane is rewritten with that many
pulses to the akṣara, the first of the cycle accented. The name says which, as in "ādi tāḷam,
khaṇḍa naḍai". Laya and naḍai go together, so ādi tāḷam in tiśra naḍai at duguṇ is six pulses to the
akṣara. Code: `rhythm::laya` and `rhythm::gati`.

## Rhythm generator

**Mode > rhythm > rhythm generator...** writes new grooves (owner, 2026-10-27: "a combo of
all of these as you see fit", after Music Pattern Generator, Stochas and Subsequence). Any rhythm's
dialog also has **edit in the generator...**, which opens the generator with that rhythm's sounds as
written lanes, at the laya and naḍai chosen.

Each row of the table is a lane: a General MIDI drum and its own cycle.

- **steps**, **a beat**, **pulses** and **rotation** make a Euclidean rhythm. The pulses are spread
  as evenly as they go over the steps by Bjorklund's algorithm, then the cycle is turned. Toussaint
  (2005) showed that many traditional timelines come out this way: E(3,8) is the tresillo, E(5,8)
  the cinquillo, E(7,12) the bembé bell, E(5,16) the bossa nova.
- **written hits** (X accent, x hit, o soft, . rest) take the Euclidean rhythm's place.
- **sieve** is a Xenakis sieve over the lane's steps, as in the stochastic tools, such as 4@0|4@3.
  Only the steps it holds may play.
- **chance** is how likely each hit is to play, as in Stochas.
- **push ms** plays the lane late (laid back) or early (pushing).

Lanes of different lengths run against each other (polymeter). The clip lasts until they line up
again, at most 64 bars, unless you set the bars yourself. A 16-step lane against a 12-step lane
comes round in three bars of 4/4.

The rules come from Subsequence:

- a fill in the last bar of every n bars, where rests play softly by chance
- a density that rises or falls across the clip
- every nth hit left out
- each lane turning a step later every n bars

The feel settings are swing (in the MPC's percentages: 50% is straight, 66% a triplet shuffle) on
each lane's own steps, on 8ths or on 16ths, and humanized timing and loudness. Swing bends the time
inside each swung pair, so 16ths between swung 8ths follow along. The **feel...** menu fills these
in. It has straight, 16th swing at 54, 58 and 62%, triplet shuffle, laid back (the snare behind the
beat), pushing (the cymbals ahead), loose (dilla-style), second line and human. These are our own
starting points, after Hydrogen's swing and humanizing. They are not measurements of any drummer.

Every chance is drawn from the **seed** with the stochastic tools' random numbers, so the same
settings and seed always write the same groove. **new** picks another seed. **start from...** loads
one of ten Euclidean rhythms from Toussaint (2005), each with a low drum on the beat.

The groove goes to the mixer as a MIDI clip, to a new Tracker pattern (on the grid, without swing
or humanizing, up to 256 rows), or to a .mid file. The settings are kept in `settings.ini` for next
time. Code: `Source/mixer/RhythmGenerator.{h,cpp}` and `Source/ui/RhythmGeneratorDialog.cpp`. The
tests are in `testRhythmGenerator`.

## My breaks

**Mode > rhythm > my breaks** holds your own breaks, added as needed (owner, 2026-10-27). Use
**add break files...**, or **add a folder of breaks...** to take every audio file in a folder.
Each file is transcribed with Drums to MIDI's hit finder (`mixer::breakRhythm`):

- The tempo comes from the loop's length and hits (`beatgrid::fromLoop`). The file is read as 1
  to 16 bars of 4/4, and the slowest tempo from 60 to 200 bpm whose 16ths the hits sit on is the
  one used.
- Each hit goes on its nearest 16th as a kick, snare or closed hi-hat.
- Hits at velocity 110 and above are accents. Hits under 70 are ghost notes.

Each break is named after its file, in lower case, and works like any other rhythmic mode: as a
mixer clip, a Tracker pattern or a .mid file.

A file that is not cut to whole bars is left out, and the dialog says why. So is a file with no
drum hits.

The transcriptions are kept in `breaks.json` beside `settings.ini`, so the audio files are only
read once. **remove** takes one break off the list, or all of them.

## East Asia: korean jangdan (7)

These are Korean jangdan on the janggu:

- jungmori
- jungjungmori
- jajinmori
- gutgeori
- semachi
- eotmori (3+2+3+2)
- hwimori (danmori is hwimori played faster)

The strokes are played like this:

| stroke | how it is played | General MIDI sound |
|---|---|---|
| kung | the left head (gungpyeon) | low conga |
| deok | the right, the stick (chaepyeon) | high timbale |
| deong | both heads together | both of the above |
| gi | a grace note before deok | soft high timbale |
| deoreoreo | a roll | soft high timbale |

Jinyangjo, the slowest jangdan (24 pulses), is left out because its strokes vary too much between
sources. Japanese and Chinese rhythms are not included yet: for example taiko ji-uchi, matsuri
bayashi, and the luógǔ jīng of Peking opera. Each would need a source the owner trusts.

## Sources

North Indian tāl:

- Martin Clayton, *Time in Indian Music: Rhythm, Metre, and Form in North Indian Rāg Performance*
  (Oxford University Press, 2000): tāl structures, vibhāgs, ṭhekās.
- Robert S. Gottlieb, *Solo Tabla Drumming of North India* (Motilal Banarsidass, 1993).
- David R. Courtney, *Fundamentals of Tabla* (Sur Sangeet Services, 1995): ṭhekās and bols.
- Vishnu Narayan Bhatkhande, *Kramik Pustak Mālikā*: the notation signs (x, 0, tālī numbers).

South Indian tāḷam:

- P. Sambamoorthy, *South Indian Music*, books I–III (Indian Music Publishing House): the
  sūḷādi sapta tāḷas, jātis, kriyās.
- T. Viswanathan and Matthew Harp Allen, *Music in South India* (Oxford University Press, 2004).
- David P. Nelson, *Solkattu Manual* (Wesleyan University Press, 2008): gati and solkaṭṭu.

Medieval rhythmic modes:

- Johannes de Garlandia, *De mensurabili musica* (c. 1240), edited by Erich Reimer (Steiner,
  1972).
- Richard H. Hoppin, *Medieval Music* (Norton, 1978).
- William G. Waite, *The Rhythm of Twelfth-Century Polyphony* (Yale University Press, 1954).

Timelines and claves:

- Godfried T. Toussaint, *The Geometry of Musical Rhythm* (CRC Press, 2nd ed. 2019).
- Godfried T. Toussaint, "A Mathematical Analysis of African, Brazilian, and Cuban Clave Rhythms"
  (Bridges, 2002): son, rumba, gahu, shiko, soukous, bossa nova, and the 12/8 standard pattern.
- David Locke, *Drum Gahu* (White Cliffs Media, 1987).
- Kofi Agawu, *The African Imagination in Music* (Oxford University Press, 2016).

The rhythm generator:

- Godfried T. Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms"
  (Bridges, 2005): Euclidean rhythms and their traditional timelines.
- E. Bjorklund, "The Theory of Rep-Rate Pattern Generation in the SNS Timing System" (SNS-NOTE-CNTRL-99,
  Los Alamos, 2003): the algorithm.
- Iannis Xenakis, *Formalized Music* (Pendragon, rev. ed. 1992): sieves.
- Roger Linn's swing percentages, from the LinnDrum and the Akai MPC60.
- Hydrogen (hydrogen-music.org), Stochas (Surge Synth Team) and Subsequence: the ideas of swing and
  humanizing, chance per step, and rules. No code is taken from them.

Caribbean and South America:

- Ed Uribe, *The Essence of Afro-Cuban Percussion and Drum Set* (Warner Bros., 1996).
- Ed Uribe, *The Essence of Brazilian Percussion and Drum Set* (Warner Bros., 1993).
- Rebeca Mauleón, *Salsa Guidebook* (Sher Music, 1993): claves and the cáscara.
- "Huayno", Wikipedia: the stressed beat and two short ones.
- Michael E. Veal, *Dub* (Wesleyan University Press, 2007): one drop, rockers, steppers.

Arabic and Maghrebi rhythms:

- A. J. Racy, *Making Music in the Arab World* (Cambridge University Press, 2003).
- Deborah Kapchan, *Traveling Spirit Masters* (Wesleyan University Press, 2007): Gnawa.
- [Maqam World, iqāʿāt](https://ftp.maqamworld.com/en/iqaa.php), and
  [samāʿī thaqīl counted in 10](https://www.oudforguitarists.com/count-to-10-samai-rhythm-samai-thaqil/)
  (beats 1, 4, 6, 7 and 8 accented, 3+2+2+3).

Korean jangdan:

- [Traditional Korean rhythm](https://en.wikipedia.org/wiki/Traditional_Korean_rhythm) (Wikipedia)
  and [국악/장단](https://en.namu.wiki/w/%EA%B5%AD%EC%95%85/%EC%9E%A5%EB%8B%A8) (Namuwiki): the stroke
  tables used here.
- Keith Howard, *Korean Musical Instruments* (Oxford University Press, 1995).

Breakbeats: the records named with each one.
