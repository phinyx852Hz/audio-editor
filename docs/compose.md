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

### South and West Asia

- **Rāga Development** (Hindustani). The ālāp unfolds the rāga by badhat, a note more of range per
  phrase group from mandra Sa, resting on the vādī, Sa and the samvādī, with kaṇ graces and the
  pakaḍ. Then come joṛ with a pulse, jhālā with the chikārī, and a gat in tīntāl (theka on the
  tablā, sthāyī, antarā and toṛā) ending in a tihāī on sam. The tānpūrā, sāraṅgī, swarmaṇḍal and
  drone play beneath. By default it uses the rāga chosen in the Mode menu, else Yaman.
- **Maqām: Taqsīm and Waṣla** (Arab). The taqsīm moves through the ajnās: the lower jins, the
  ghammāz, the upper jins and the jawāb, a modulation, then the descent and qafla, often in
  sequence. Its sections grow longer, with the longest and the modulation in the second half.
  Each opens on a set figure (a rising fourth, or three falling notes) and closes on a qafla of
  three falling notes and a silence. The drone is heard early in broken strokes, then stops, and
  the line moves from a near-metre to free rhythm (after Nettl and Riddle, 1973, and Marcus).
  Then comes a waṣla:
  - a samāʿī thaqīl (10/8) in four khānāt with its taslīm, the fourth khāna in samāʿī dārij;
  - a layālī, the voice free on "yā layl, yā ʿayn", each phrase answered by the ʿūd, with no drums;
  - a muwashshaḥ in maqsūm (dawr, dawr, khāna, qafla);
  - the closing qafla.

  The qānūn, nāy and kamān play in heterophony.
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
- **Congolese Guitar: Rumba and Sebene** (Congolese rumba and soukous, Kinshasa and Brazzaville).
  Three guitars interlock on a short cycle of I, IV and V, built on the mode's own degrees. The
  accompagnement repeats a figure that arpeggiates each chord. The mi-solo plays only in its gaps,
  in the middle register, some notes in thirds. The solo plays short cells above both. First comes
  a sung rumba: a verse with the voices in thirds, then a refrain in call and response, the solo
  answering each phrase. A break leads into the sebene, where the cycle repeats over the cavacha,
  the snare's running figure. The sebene builds in four stages. The accompagnement and mi-solo lock
  together. Then the solo calls and the mi-solo answers. Then the solo's cell varies a little each
  cycle while the voices shout to the dancers. Last comes the climax, higher and denser. All close
  together on the tonic. The parts are the solo, mi-solo, accompagnement, bass, drums, lead voice,
  second voice and congas.

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
Locke and Agawu (Ewe); Arom (Central African polyphony); and Stewart, White and Eyre (Congolese
rumba). No other program's code is used.
