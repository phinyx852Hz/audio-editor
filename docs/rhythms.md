# Rhythmic modes

The owner asked on 2026-10-26 for "a submenu for rhythmic modes, that would include all north
indian tala, south indian talam, all classic breakbeats, trap beats, medieval european rhythmic
modes, and of course, Caribbean, south american, and west and north african rhythms, and if
there's anything from East Asia, that would be great". Every name is lower case and in proper
transliteration (owner).

The menu is **Mode > rhythmic modes**. It has one submenu for each tradition. Hovering over a
rhythm shows what it is and how it is counted: the ṭhekā's bols, the kriyās, or the strokes.
Picking a rhythm opens a dialog with its sounds, the number of cycles, and a tempo, which starts
at a usual tempo for that rhythm. The dialog has three buttons:

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

## North Indian tāl (16)

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

## South Indian tāḷam (39)

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

## Breakbeats (10)

amen break, funky drummer, think (about it), apache, impeach the president, when the levee breaks,
funky president, cold sweat, it's a new day, the big beat.

Each one is the main bar (the amen break has four bars) as drum machines are usually programmed from
the record. They are simplified: the drummers vary every bar, and ghost notes are included only
where they define the groove. Each one names its record and year, and its drummer where that is
documented. These are our own transcriptions of rhythms, not samples.

## Trap (3)

| rhythm | what it has |
|---|---|
| trap (half-time) | the clap on the third beat, 808 kicks, 16th hats rolling into 32nds |
| trap (triplet hats) | the hats in 16th-note triplets |
| drill | swung triplet hats, and the snare on the third beat and late in the second bar |

All three are at 140 to 142 bpm.

## Medieval rhythmic modes (6)

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

## Caribbean (17)

son clave (3-2 and 2-3), rumba clave (3-2 and 2-3), tresillo, cinquillo, habanera, son montuno
(tumbao, 2-3), cha-cha-chá, bembé (6/8), one drop, rockers, steppers, ska, dembow, soca, merengue.

## South American (7)

samba (batucada), bossa nova, baião, tango (3-3-2), milonga, candombe, cumbia.

Still to come: chacarera, maracatu, ijexá, partido alto, festejo and joropo.

## West African (6) and North African (6)

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

## Middle Eastern (1)

samāʿī thaqīl is the 10/8 of the samāʿī, grouped 3+2+2+3. Dum falls on eighths 1, 6 and 7, and
tak on 4 and 8.

Not included yet: çiftetelli, wahda w-nuṣṣ and the other long Arab and Ottoman cycles. They can be
added from Maqam World's iqāʿ pages once each one is checked.

## My breaks

**rhythmic modes > my breaks** holds your own breaks, added as needed (owner, 2026-10-27). Use
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

## East Asian (7)

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

Caribbean and South America:

- Ed Uribe, *The Essence of Afro-Cuban Percussion and Drum Set* (Warner Bros., 1996).
- Ed Uribe, *The Essence of Brazilian Percussion and Drum Set* (Warner Bros., 1993).
- Rebeca Mauleón, *Salsa Guidebook* (Sher Music, 1993).
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
