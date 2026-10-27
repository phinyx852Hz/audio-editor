# World modes and tunings

The owner's research (Mode Research, 2026-10-26) lists the modal and tuning systems of the world.
Each one is now in PHI's single list of systems, `mixer::tuningSystems()` (`Source/mixer/Tunings.cpp`
and `Source/mixer/WorldModes.cpp`). The Mode menu, the Inspector's tuning, counterpoint, the fugue,
stochasticize, cubify, the MIDI editor and the PHI synth all read that one list. A system added
there is everywhere at once.

## How it is organized

Every system has three things besides its pitches:

- **kinds**: *melodic* (a mode, scale, rāga, maqām or pathet), *tuning* (a temperament, a just or
  equal tuning, a laras), or both. Gamelan laras, mbira tunings and the Greek genera are both.
- **from**: the people and the traditions it belongs to: composers and theorists, and nations,
  cultures and empires. A system can belong to several. Lydian is under the western church,
  Zarlino, Glarean, Beethoven, Chopin, Mahler, Janáček, Lippius and Crüger, Roy Harris and
  George Russell.
- **period**: what it repeats at. Most repeat at the octave. Hacıbəyli's mugam chains and the
  Russian obikhod repeat at a fourth, Bohlen-Pierce at the tritave (3/1), and Carlos's alpha,
  beta and gamma at a single step.

Each person or tradition is an `Origin`, with a region and a date. The regions are Europe, West
Asia, Central Asia, South Asia, Southeast Asia, East Asia, Africa, the Americas and Oceania.
Within a region, traditions come first and then people, each in order of date.

## Where it shows

- **Mode ▸ melodic**: region ▸ tradition or person ▸ modes. The mode chosen, and the region and
  origin it sits under, are in bold.
- **Mode ▸ tuning**: the same tree for tunings, plus *equal divisions* (2 to 24, 31, 36, 41, 48,
  53, 72 and 96). *The mode's own tuning* is the default. A tuning chosen here is what **tune the
  track** gives the PHI synth. The mode still decides the notes for *notes into the mode*,
  counterpoint and the fugue. A temperament (Werckmeister III, Kirnberger III, Zarlino's
  meantone) is laid on C whatever the final, as it is on a keyboard. Other tunings start on the
  final.
- **Mode ▸ find a mode or tuning...**: a search over names, plain names, families, people,
  cultures, regions and notes. "partch", "ottoman", "west asia" and "genus" all work.
- **Inspector ▸ Tuning (PHI synth)**: each row's **…** button opens the same atlas, and a search.
- **PHI Synth**, the stock synth being built as its own project, gets each tuning as a
  `phi::Tuning` through `Instrument::setTuning`, or as MIDI Tuning Standard messages. So it plays
  every system here with nothing more to add. To browse the atlas itself, it can compile
  `Tunings.cpp` and `WorldModes.cpp`. They are plain C++ and need only `PhiTuning` and `PhiRaga`
  from phi_dsp.
- **The built-in Synth** now plays the tuning in force too, key by key, until PHI Synth takes its
  place. Before this, only PHI rāga followed the tuning.
- **The MIDI editor** (#68) builds its rows from the same list, so these systems are on its
  piano roll. A non-octave system's `period` is still to be read there.

## What was added

Europe:
- Greek octave species, in Pythagorean tuning.
- Greek genera: Archytas (3), Aristoxenus (6, in twelfths of a tone) and Ptolemy (8).
- The Pythagorean tuning.
- The eight Byzantine ēchoi, in 72 moria (Chrysanthos's genera, as the 1881 committee set them).
- The obikhod scale.
- Guido's hexachord, and Glarean's Locrian (Hyperaeolian).
- Ramos de Pareja's monochord, Zarlino's syntonic just intonation and his 2/7-comma meantone.
- Werckmeister III and Kirnberger III.
- Hungarian minor, scala enigmatica, harmonic minor, Prometheus, and the major and minor pentatonic.
- Lydian diminished (Russell), Grainger's modal folksong scale and the Andalusian modo de mi.
- Gilchrist's 15 Gaelic modes: 5 pentatonic, each in forms a and b.
- Four Jewish prayer modes (shtayger).

The Americas and Oceania:
- Partch's 43, The Well-Tuned Piano and The Harp of New Albion.
- Wilson's 1 3 5 7 hexany, and two harmonic-series modes.
- Bohlen-Pierce, equal and just; and Carlos's alpha, beta and gamma.
- Equal divisions for Ives, Carrillo, Yasser, Blackwood, Darreg, Sims and Maneri. In Europe, the
  same for Huygens, Vicentino, Fokker, Busoni, Hába and Wyschnegradsky.

West Asia:
- Zalzal's neutral diatonic, Ṣafī al-Dīn's 17 notes and Arel-Ezgi-Uzdilek's 24.
- Nikrīz, būsalīk and ʿirāq.
- Nine ajnās.
- Nine Turkish makams in 53 commas.
- Five Persian āvāz.
- Hacıbəyli's four mugam scales.

South Asia:
- The 72 melakartas (12-TET), in Govindācārya's names.
- The 22 śrutis, the two grāmas and their 14 mūrcchanās.
- Bhatkhande's ten ṭhāṭ and the Hanumat mata's six rāgas, placed on the existing raga sheets.

Southeast Asia:
- Six more Balinese patutan.
- Morton's five Thai modes in seven equal steps, which also stand for Cambodia and Laos.
- Two Vietnamese modes, from Nhan's measured cents.

East Asia: kumoijōshi. The rest of East Asia was already in the list, and is now placed under
Jing Fang, He Chengtian, Cai Yuanding, Zhu Zaiyu, Yang Yinliu, Yatsuhashi Kengyō, Uehara
Rokushirō, Koizumi Fumio and Okinawa.

Africa: the six Ethiopian qenet, beside the Shona mbira tunings already in the list.

## Not playable yet

The research names these, but gives no pitches for them, and they need a sourced pitch set before
they can sound:

- Shashmaqom's six maqoms, the Uyghur 12 muqam and the Andalusi nūbāt. These are suites; the
  modes inside each are not set out.
- Iraqi maqām beyond ʿirāq. The others are placed on the Arab maqāmāt they share.
- Ṣafī al-Dīn's 12 shudūd and 6 āvāz; al-Lādhiqī's system.
- The Turkish sabâ and beste-nigâr.
- Azerbaijani bayatı-şiraz, şüştər and humayun. Hacıbəyli names their tetrachords, but the
  English edition leaves unclear how he joins them.
- The Myanmar saung-gauk tunings (hnyin-lôn, auk-pyan, pale, myin-zaing).
- The Thai thang (pitch levels) and samniang (accent styles).
- Sundanese madenda and mandalungan; sléndro miring; the Yogyakarta manyura sub-pathet.
- Vietnamese nam xuân, oán and nam Huế.
- The Serbian osmoglasnik and the Syriac octoechos. These are formula systems with no one scale
  each.
- Busoni's 113 scales. His rule for which alterations he allowed isn't in the Sketch, and the
  obvious rules give 155 or 105.
- Classifications rather than scales: Hucbald's interval modes, Kolinski's tint-complexes,
  Bronson's modestar, jāti, and auḍava, ṣāḍava and sampūrṇa.

Some entries are readings rather than measurements, and the menu's tooltip says so: the Persian
āvāz read on their dastgāh's notes; the Turkish segâh and ırak read on rast's notes; and the
Balinese patutan, read on the Javanese pélog.

## Sources

- The owner's research, sections 1 to 5.
- Ptolemy, *Harmonics* II.14, for the genera.
- Chrysanthos, *Theōrētikon mega* (1832), and the Patriarchal Music Committee (1881), for the
  moria.
- Hacıbəyli, *Principles of Azerbaijani Folk Music* (1945), via musbook.musigi-dunya.az.
- Nhan, *Journal of Social Sciences and Humanities* 3(5), 2017, for the Vietnamese cents.
- The Scala archive's published ratios for Partch, Young, Riley and Ramos.
- Werckmeister, *Musicalische Temperatur* (1691), and Kirnberger, *Die Kunst des reinen Satzes*
  (1771–79).
- Morton, *The Traditional Music of Thailand* (1976).
