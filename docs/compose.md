# Compose

Status: built 2026-10-29. The Compose menu gathers every method of composing PHI has. It takes the
place of the Architectonics menu, which is now a submenu of it.

## What the owner asked for (2026-10-29)

"Should we have a Compose menu instead? that would include all the compositional methods available
in Phi ... I want to include non-Western compositional approaches as well related to any type of
music as identified by musicologists and ethnomusicologists. Composing is sometimes fused with
"production" but the idea of composing is definitely related to organization and structure. in that
compose menu would also be a choice to open the microtonal editor (Staff/MIDI) to start from a blank
canvas. Again, we are going to be limited to 8 parts for the 8 tracks maximum available in Phi."
Then: the notation systems belong in Compose too, PHI's own microtonal staff the default, and no
"world" label.

## The menu

- **New Microtonal Score...** opens the MIDI editor on four empty bars in the Mode menu's tuning, on
  PHI's microtonal staff. The Staff/MIDI toggle switches to the piano roll.
- **New Score In** opens the same blank score in any other notation: each tradition's own,
  Hindustani sargam (Bhatkhande), Carnatic swaras, jianpu, kepatihan, jeongganbo, Arabic maqam staff,
  Turkish makam staff (AEU), Persian dastgah staff (koron and sori), or degree numbers: the MIDI
  editor's staff view and notation::systems() (mixer/Notation.h).
- **Compose...** is one dialog for all the methods below. Choose the method, the parts (never more
  than eight), about how long, the tempo and the pitches: the Mode menu's mode, or the tradition's
  own tuning. It lists the sections it wrote. **Deal Again** writes the same form with other notes.
  **Open the Score** opens it in the MIDI editor, a part a channel and the sections marked, and a
  kept score goes on a track. **Save as MIDI...** writes the file.
- **Architectonics**: Compose a Song, Compose a Score, Design a Set, Play the Set, Deal a New Set and
  Endless ([architectonics.md](architectonics.md)).
- The methods by region, oldest first. They are listed below.
- **Rhythm Generator...**: Euclidean timelines against each other ([rhythms.md](rhythms.md)).

The methods PHI had before Compose are still in the Mode menu and on the mixer's right-click menus.
Counterpoint and the fugue work on the MIDI clip shown in the mixer when there is one. Otherwise
they write a cantus firmus or a subject of their own and open the Compose dialog.

## The methods

Each method writes a score from nothing, organized as its tradition organizes music: its form, its
cycles, its layers and how its parts relate. All of them are in mixer/Compose.h, in our own code.

### Europe and the Americas

- **Organum** (Notre Dame, c. 1170–1230). A chant tenor is held while the voices above it move in
  the rhythmic modes, consonant on each long. In three parts the duplum and triplum exchange their
  phrases (Pérotin's Stimmtausch). Then comes a discant clausula in two cursus, with the tenor in
  the fifth mode, and the close on the final.
- **Isorhythm** (Ars nova, 14th c.). The tenor's talea and color have different lengths, so that
  two colors fill three taleae. The second half is in diminution. The upper voices repeat taleae
  of their own (panisorhythm) with hockets at the joins.
- **Species Counterpoint** (Fux, 1725). A cantus firmus is written by Fux's rules, then up to
  three voices against it in each species in turn.
- **Fugue**. A two-bar subject, then the fugue of mixer/Fugue.h in two to four voices.
- **12-Tone Row** ([serial.md](serial.md)).
- **Stochastic Matrix** (Xenakis, Achorripsis, 1957). The cells of a matrix of timbres by sections
  hold 0 to 4 events, in counts that follow Poisson's law (λ 0.6). Each cell is a cloud of notes
  by the laws of Formalized Music, on the mode's degrees.
- **Stochasticize the Clip** and **Cubify** ([stochastic.md](stochastic.md), [cubify.md](cubify.md)).
- **Chance and Aleatory**. Cage's chance operations: each sound, length and dynamic is cast from
  charts of 64 by the I Ching (three coins six times). A moving hexagram replaces its sound in the
  chart. The sections are in Music of Changes' square-root proportions. Then comes Lutosławski's
  ad libitum, where every part loops its own figure out of time until a cue, a battuta.
- **4′33″ (Silence)** (Cage, 1952). Three movements, all tacet, in the lengths of the premiere
  (David Tudor, Woodstock, 1952): 33″, 2′40″ and 1′20″ at the tempo, 4′33″ in all, whatever the
  Length. The performer closes the lid to begin each movement and opens it to end it. What sounds is
  the place, by chance (Poisson arrivals, each layer at its own rate, all quiet): the room, the
  audience, the street, rain in the second movement and wind in the first, and the high and low
  sounds Cage heard in the anechoic chamber (his nervous system and his circulation).
- **Star Charts** (Cage, Atlas Eclipticalis, 1961–62). Each part has a page of a star atlas laid
  over its staff a page at a time: across is time, up is pitch (on the mode's degrees, in the part's
  register), each star a sound. Stars come in constellations on a scattered field, faint ones about
  2.5 times as many as the magnitude before; the brighter the star, the louder and longer its sound,
  and a player may leave out a faint one. The sections are the pages, at the conductor's clock.
- **Musicircus** (Cage, 1967). Many musics at once and none of them coordinated. Each part is one
  part of another Compose method, at a tempo of its own (0.6 to 1.5 times the score's), coming in
  and going out by chance one to three times; while it is out its music goes on unheard. Only the
  methods in the Mode menu's mode take part (none with a tradition's own tuning, and not Spectral).
  The sections mark each entrance and exit.
- **Process Music** (Reich and Glass). A figure, Piano Phase's pattern (one copy phasing against
  one held), a pulse, a bass built up, a counter-figure in subtractive process, a shorter cycle and
  an arpeggio are each given to a part through minimalize's processes.
- **Minimalize a Scene**: the launcher's scene at the cursor.
- **Tintinnabuli** (Pärt). An M-voice moves by step around a centre, its phrases growing by a note.
  A T-voice plays the tonic triad's nearest note above it. Then come their mirror, a bell, and a
  mensuration canon at half and a quarter of the speed, each an octave lower.
- **Spectral** (Grisey, Murail). Each part is a partial of a low fundamental's harmonic series, in
  its exact cents (sent as pitch bend, so the score plays in 12-TET with the bends). Each cycle is an
  attack and its resonance. The spectrum stretches from harmonic to most inharmonic at the golden
  section, and back.

- **Tuning Meditation** (Pauline Oliveros, first sung at the Guggenheim in 1981 by 75 singers under
  William Duckworth). The score is followed as written, a breath at a time, each voice at its own
  pace: breathe out with air sound, listen with the mind's ear for a tone, sing it on one breath,
  listen to the whole field, tune as exactly as possible to a distant voice, listen again, sing a
  new tone no one else is singing (louder, to be heard far off), then go on listening and singing a
  tone of one's own or tuning to another's, by turns. One tone a breath. Each voice tunes to the
  most distant voice it has not yet tuned to, so it reaches as many as it can, in its own register.
  Everyone ends together. The sections mark when the first voice reaches each step.
- **Time Brackets** (Cage, the Number Pieces, 1987–92). Each part is a row of time brackets: a
  window to start in and an overlapping window to stop in (0′00″↔0′45″, 0′30″↔1′15″, shorter in a
  short piece), staggered between the parts. Each holds one quiet held sound, or two or three, cast
  by the I Ching from a gamut of five to nine of the mode's degrees; about one bracket in five is
  empty. The percussion plays cymbal, bell, tom and tambourine.
- **Deep Listening** (Oliveros and the Deep Listening Band, 1989, in the Fort Worden cistern).
  Oliveros's sonic awareness: attention moves between global (the circle: everyone holding long,
  overlapping tones that blend in the space) and focal (the dot: one player's phrase standing out
  over the others holding the tonal centre, very soft). In the circle each tone is one of its
  quarters: making a sound, imagining one (a silence), listening to the sound present (joining the
  tonal centre), or remembering a past one. The accordion, trombone, voice and didjeridu play; with
  more parts the Expanded Instrument System repeats the accordion and the trombone after 1.5, 3.7
  and 6.1 seconds, each repeat softer, the cistern rings on after every focal note, and a conch
  sounds at each turn to the circle.

### South and West Asia

- **Rāga Development** (Hindustani). The ālāp unfolds the rāga by badhat, a note more of range per
  phrase group from mandra Sa, resting on the vādī, Sa and the samvādī, with kaṇ graces and the
  pakaḍ. Then come joṛ with a pulse, jhālā with the chikārī, and a gat in tīntāl (theka on the
  tablā, sthāyī, antarā and toṛā) ending in a tihāī on sam. The tānpūrā, sāraṅgī, swarmaṇḍal and
  drone play beneath. By default it uses the rāga chosen in the Mode menu, else Yaman.
- **Maqām: Taqsīm and Waṣla** (Arab). The taqsīm moves through the ajnās: the lower jins, the
  ghammāz, the upper jins and the jawāb, a modulation, then the descent and qafla, often in
  sequence. Then comes a waṣla: a samāʿī thaqīl (10/8) in four khānāt with its taslīm (the fourth
  in samāʿī dārij), a muwashshaḥ in maqsūm (dawr, dawr, khāna, qafla), and the closing qafla. The
  qānūn, nāy and kamān play in heterophony.
- **Radif and Dastgāh** (Persian). A pishdarāmad, then the gushe-hā from the darāmad to the owj,
  each on a higher shāhed with its own ist. Kereshmeh is in its 6/8 rhythm, and the forud returns
  home. The voice's phrases are answered by the tār (javāb). A chahārmezrāb and a reng close it.

### East and Southeast Asia

- **Gamelan Colotomic Form** (Central Java). A ladrang: a balungan in gatra of four, each ending on
  its seleh, with the gong cycle of 32 beats marked. The kenong falls on 8, 16, 24 and 32, the
  kempul on 12, 20 and 28, the gong on 32, and the kethuk and kempyang between. Saron peking and
  bonang barung (mipil) elaborate it, twice as densely in irama II, with the ngelik in longer
  pieces. A buka begins it and a suwuk slows to the last gong.
- **Jo-ha-kyū** (gagaku and nō, after Zeami). The jo is unmetred (netori). The ha is metred,
  quickening from half speed. The kyū is fast, with the flute alone at the close (tomede). The
  ryūteki and hichiriki play in heterophony (the hichiriki's enbai), over the shō's aitake
  clusters, the koto and biwa, and the kakko's katarai, taiko and shōko.
- **Sanjo** (Korean). A solo over the janggu through jinyangjo, jungmori, jungjungmori, jajinmori
  and hwimori, slow to fast. Each cycle is tied in tension (maeggo) and released (pulgo). With more
  parts it becomes a sanjo hapju, each instrument playing its own version of the line.
- **Sǎn-màn-zhōng-kuài-sǎn** (Chinese). One qupai in four phrases, qǐ, chéng, zhuǎn and hé, each
  beginning on the note the last ended on (yú yǎo wěi). It is played free, then slow with added
  flowers (jiāhuā), moderate and fast (banshi variation), and free again.

### Africa

- **Layered Ostinato** (Ewe, Ghana and Togo). The gankogui's twelve-pulse standard pattern, then
  the axatse, kaganu, kidi and sogo enter one by one. The atsimevu's calls switch the kidi and
  sogo between responses of three against four and two against three, and a cantor and chorus
  sing in call and response.
- **Hocket and Interlocking** (Banda-Linda horns, Aka; Arom). A cyclic melody of 24 pulses is shared
  out note by note, one pitch a player, one player a pulse. Then each figure varies while the
  melody stays.

## Eight parts

Every method has at most eight parts, the mixer's eight tracks. So do the methods PHI had:
Architectonics' eight colours, counterpoint's cantus and three voices, the fugue's four voices,
the twelve-tone row's four, minimalize's eight tracks and cubify's eight.

## Sources

The sources for each method are listed in mixer/Compose.cpp's methods() and shown in the dialog.
They include Roesner and Anonymous IV (organum), Bent (isorhythm), Fux, Gédalge, Xenakis's
Formalized Music, Pritchett on Cage, Reich's "Music as a Gradual Process", Hillier on Pärt, and
Fineberg on spectral music. For Asia and Africa: Jairazbhoy, Bor and Clayton (rāga); Marcus, Racy
and Touma (maqām); During, Nettl and Farhat (radif); Becker, Sumarsam and Pickvance (gamelan);
Zeami, Malm and Garfias (gagaku); Howard and Provine (sanjo); Witzleben, Lau and Jones (Chinese);
Locke and Agawu (Ewe); and Arom (Central African polyphony). No other program's code is used.
