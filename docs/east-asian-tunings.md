# Chinese, Japanese and Korean tunings and modes

Added to the modal tunings for the PHI synth (owner, 2026-10-26: "ancient and current Chinese
modes and tuning systems based on ancient Chinese treatises and current ones if available. Same
goes for Japanese modal systems"). They are in `Source/mixer/Tunings.cpp`, after the dastgahs,
and play as the others do ([mixer.md](mixer.md), "Modal tunings"): pick one for a clip, scene or
track, and a root. Names are pinyin with its tones and Hepburn with its long vowels, in lower case
(owner), with a plain ASCII name beside each that a mix saves. Each source is below its entry.

## Chinese tunings (family "chinese tuning")

| Name (plain) | What it plays |
| --- | --- |
| shí'èr lǜ (sānfēn sǔnyì) (`shier lu (sanfen sunyi)`) | The twelve lü by "taking away a third, adding a third" of the pipe's length: eleven pure fifths (3/2) up from huángzhōng, folded into the octave. Zhònglǚ, the last, is 177147/131072 (521.5 cents), a Pythagorean comma above the fourth, so the chain does not lead back to huángzhōng. On the keys from the root: huángzhōng, dàlǚ, tàicù, jiāzhōng, gūxiǎn, zhònglǚ, ruíbīn, línzhōng, yízé, nánlǚ, wúyì, yìngzhōng. |
| jīng fáng liùshí lǜ (`jing fang 60 lu`) | Jīng Fáng's (78–37 BCE) answer: the chain carried on to sixty notes. The 54th, sèyù, is 3.6 cents above huángzhōng (the 53-fifth comma, which Jing Fang called "one day"). Sixty notes to the octave, a note a key up and down from the root. |
| cài yuándìng shíbā lǜ (`cai yuanding 18 lu`) | Cài Yuándìng's (1135–1198) eighteen lü: the twelve and six biànlǜ, the chain six fifths further, so a mode on any lü keeps its intervals. Eighteen to the octave, a note a key. |
| hé chéngtiān xīnlǜ (`he chengtian xinlu`) | Hé Chéngtiān's "new lü" (c. 443 CE): the pipe a fifth above zhònglǚ falls short of half huángzhōng's length; that shortfall is cut into twelve equal parts, and the kth lü of the chain gets k parts added to its length, so the twelfth closes on the octave. Equal steps of length, not of pitch: each lü is within 8 cents of 12-TET. |
| zhū zàiyù xīnfǎ mìlǜ (`zhu zaiyu xinfa milu`) | Zhū Zàiyù's "new method, close ratio" (1584): twelve equal steps, the ratio 2^(1/12) worked to 25 digits. The same pitches as 12-TET (and as Nakane Genkei's equal temperament in Japan, *Ritsugen hakki*, 1692). |
| chúnlǜ wǔshēng (qín) (`chunlu wusheng (qin)`) | Just intonation (chúnlǜ), the third of the "three lü" of Chinese theory beside sānfēn sǔnyì and equal temperament: gōng, shāng 9/8, jué 5/4, zhǐ 3/2, yǔ 5/3, as the guqin's harmonics at its hui sound them. |

Sources:
- *Guǎnzǐ*, "Dìyuán" (地員): the five notes by sānfēn sǔnyì, the oldest account.
- *Lǚshì chūnqiū* (239 BCE), "Yīnlǜ" (音律): the twelve lü and their order of generation; also *Shǐjì*, "Lǜshū" (律書).
- *Xù Hànshū* (in *Hòu Hànshū*), "Lǜlì zhì" (律曆志): Jing Fang's sixty lü; Qián Lèzhī's (5th c.) 360 lü, the chain carried further still, are recorded in the *Suí shū*, "Lǜlì zhì" (too many for the keys, so not listed).
- *Suí shū*, "Lǜlì zhì" (律曆志): He Chengtian's new lü.
- Cài Yuándìng, *Lǜlǚ xīnshū* (律呂新書, c. 1187).
- Zhū Zàiyù, *Lǜxué xīnshuō* (律學新說, 1584), *Lǜlǚ jīngyì* (律呂精義, 1596), in *Yuèlǜ quánshū* (樂律全書, 1606).
- Kenneth Robinson, *A Critical Study of Chu Tsai-yü's Contribution to the Theory of Equal Temperament in Chinese Music* (Steiner, 1980).
- Joseph Needham, *Science and Civilisation in China*, vol. 4 part 1 (Cambridge, 1962), section 26(h) on acoustics: sanfen sunyi, Jing Fang, He Chengtian, Zhu Zaiyu.
- Yáng Yīnliú (楊蔭瀏), *Zhōngguó gǔdài yīnyuè shǐgǎo* (中國古代音樂史稿, 1981); *Zhōngguó yīnyuè cídiǎn* (中國音樂詞典, 1985), entries on the lü, chúnlǜ and the three scales below.
- Fritz A. Kuttner, *The Archaeology of Music in Ancient China* (Paragon, 1990).

## Chinese modes (families "chinese mode" and "chinese mode, equal tempered")

The five notes (wǔshēng) gōng, shāng, jué, zhǐ, yǔ (C D E G A on gōng C), and a mode on each:
**gōng** (0 2 4 7 9 in semitones), **shāng** (0 2 5 7 10), **jué** (0 3 5 8 10), **zhǐ** (0 2 5 7 9),
**yǔ** (0 3 5 7 10). Then the three seven-note scales (qīshēng) of Chinese theory, each with a
mode on each of the five notes (`yayue shang`, `yanyue zhi` and so on):

- **yǎyuè** (雅樂, "court music", also the "ancient scale"): the five with biànzhǐ (F♯) and biàngōng (B), C D E F♯ G A B.
- **qīngyuè** (清樂, "new scale"): with qīngjué (F) and biàngōng (B), C D E F G A B.
- **yànyuè** (燕樂, "banquet music"): with qīngjué (F) and rùn (B♭), C D E F G A B♭.

All in sānfēn sǔnyì (a note is its count of fifths from gōng), as the treatises build them; and
each again in 12-TET, as conservatories and modern Chinese orchestras tune (`gong (equal tempered)`).
The names of the three scales follow the *Dictionary of Chinese Music*; some authors use others.

Sources: *Guǎnzǐ*, "Dìyuán"; *Zuǒzhuàn* and *Guóyǔ*, "Zhōuyǔ" (the seven notes); Yang Yinliu
(1981); *Zhōngguó yīnyuè cídiǎn* (1985); Liang Mingyue, *Music of the Billion: An Introduction to
Chinese Musical Culture* (Heinrichshofen, 1985).

## Chinese regional modes (family "chinese regional mode")

Current practice in Guangdong, where some notes sit between the keys (approximated to 50 cents):

- **qīngsān qīngliù (cháozhōu)**: Chaozhou's "light 3 and 6", la and mi pressed lightly on the zheng: the five notes as gōng (0 2 4 7 9).
- **zhòngsān zhòngliù (cháozhōu)**: "heavy 3 and 6": a heavy press raises la and mi three quarters of a tone, so a low 7 and a high 4 replace them (0 200 550 700 1050 cents from do). Huówǔ ("living 5"), its re wavering, has the same notes and is not listed, since a fixed tuning cannot hold the waver.
- **yǐfǎn (guǎngdōng)**: Cantonese music and opera's yǐfǎn on sol, its yǐ (si) low and fǎn (fa) high: sol, low si, do, re, high fa (0 350 500 700 1050 cents).

Sources: Mercedes M. Dujunco, "Birth of a New Mode? ...", *Ethnomusicology OnLine* 8
(umbc.edu/eol/8/dujunco); Alan R. Thrasher, "Hakka-Chaozhou Instrumental Repertoire: An Analytic
Perspective on Modal Construction", *Asian Music* 19/2 (1988); Bell Yung, *Cantonese Opera:
Performance as Creative Process* (Cambridge, 1989).

## Gagaku modes (family "gagaku mode")

The six modes of tōgaku (rokuchōshi), seven notes each, in sanbun son'eki (Pythagorean) tuning, as
the twelve ritsu came from China. **Ryo** is gōng's scale with its two exchange notes (Lydian);
**ritsu** is shāng's (Dorian), its five core notes 0 2 5 7 9:

| Mode (plain) | Scale | Its customary final |
| --- | --- | --- |
| ichikotsuchō (`ichikotsucho`) | ryo | D (ichikotsu) |
| hyōjō (`hyojo`) | ritsu | E (hyōjō) |
| sōjō (`sojo`) | ryo | G (sōjō) |
| ōshikichō (`oshikicho`) | ritsu | A (ōshiki) |
| banshikichō (`banshikicho`) | ritsu | B (banshiki) |
| taishikichō (`taishikicho`) | ryo | E (hyōjō) |

The final is the root chosen with the mode. The twelve ritsu from D: ichikotsu, tangin, hyōjō,
shōzetsu, shimomu, sōjō, fushō, ōshiki, rankei, banshiki, shinsen, kamimu.

Sources: Robert Garfias, *Music of a Thousand Autumns: The Tōgaku Style of Japanese Court Music*
(California, 1975); William P. Malm, *Traditional Japanese Music and Musical Instruments* (1959;
Kodansha, 2000); Eta Harich-Schneider, *A History of Japanese Music* (Oxford, 1973).

## Japanese modes (family "japanese mode")

In 12-TET, as they are written and taught today:

- **miyako-bushi (in)** 0 1 5 7 8: Koizumi's miyako-bushi scale, the city scale of koto and shamisen music; the same notes as Uehara's in scale (insenpō) coming down.
- **in (ascending)** 0 1 5 7 10: Uehara's in scale going up.
- **ritsu (yō)** 0 2 5 7 9: Koizumi's ritsu scale, Uehara's yō scale (yōsenpō).
- **min'yō** 0 3 5 7 10: the folk-song scale (the yō scale's second mode).
- **ryūkyū** 0 4 5 7 11: the Okinawan scale.
- **hirajōshi** 0 2 3 7 8: the scale Western books derive from the koto tuning Yatsuhashi Kengyō (1614–1685) made from shamisen music; the koto tuning itself plays miyako-bushi from its first string.
- **iwato** 0 1 5 6 10: the in scale's fourth mode, after the koto tuning of that name.

Koizumi built the four scales from two tetrachords a fourth wide, each with one note inside:
miyako-bushi (a semitone above the lower nuclear tone), ritsu (a whole tone), min'yō (a minor
third) and ryūkyū (a major third).

Sources: Uehara Rokushirō (上原六四郎), *Zokugaku senritsu kō* (俗楽旋律考, 1895); Koizumi Fumio
(小泉文夫), *Nihon dentō ongaku no kenkyū* (日本伝統音楽の研究, 1958) and *Nihon no ongaku: rekishi
to riron* (1974); Burgess Speed, *Japan: Your Passport to a New World of Music* (2008) and
Kostka and Payne, *Tonal Harmony* (3rd ed., 1995) for the Western hirajōshi; Nicolas Slonimsky,
*Thesaurus of Scales and Melodic Patterns* (1947).

## Korean (families "korean tuning", "korean mode" and "korean folk mode")

Added on the owner's ask the same day ("Are there any Korean music theory and scales we can add
to this East Asian collection?"). Names are in McCune-Reischauer, the plain names in Revised
Romanization.

- **sibi-yul (sambun sonik)**: the twelve yul (십이율) of the *Akhak kwebŏm*, made by sambun sonik as the Chinese lü (the same pitches as `shier lu (sanfen sunyi)`): hwangjong, taeryŏ, t'aeju, hyŏpchong, kosŏn, chungnyŏ, yubin, imjong, ich'ik, namnyŏ, muyŏk, ŭngjong. Hwangjong is customarily about E♭, so pick E♭ as the root.
- **p'yŏngjo** (`pyeongjo`, 평조) 0 2 5 7 9, on sol: the court's bright mode (ujo in kagok and sanjo).
- **kyemyŏnjo** (`gyemyeonjo`, 계면조) 0 3 5 7 10, on la: the plaintive mode. Both in sambun sonik, as the *Akhak kwebŏm* lays them out on each yul (seven of each, the ch'iljo).
- The folk-song tori (토리), regional modes, in 12-TET:
  - **kyŏngt'ori** (`gyeongtori`) 0 2 5 7 9: Seoul and Gyeonggi, sol la do re mi on sol.
  - **menarit'ori** (`menaritori`) 0 3 5 8 10: the east (Gangwon, Gyeongsang, Hamgyeong), mi sol la do re on mi; mi shaken wide; going up it skips sol.
  - **yukchabaegit'ori** (`yukjabaegitori`) 0 5 7 8: the southwest (Jeolla), mi la (si) do on mi; mi shaken heavily, la held level, do bent down to si.
  - **susimgat'ori** (`susimgatori`) 0 2 5 7 10: the northwest (Pyeongan, Hwanghae), re mi sol la do on re; la shaken lightly.

  The shakes (yosŏng) and bends (kkŏngnŭn sori) belong to the voice and are not in the tuning.

Sources: Sŏng Hyŏn et al., *Akhak kwebŏm* (樂學軌範, 1493); Robert C. Provine, *Essays on
Sino-Korean Musicology: Early Sources for Korean Ritual Music* (Il Ji Sa, 1988); Lee Hye-ku
(이혜구), *Essays on Korean Traditional Music* (Royal Asiatic Society, 1981); Lee Bo-hyung
(이보형), whose fieldwork from the 1970s named the tori; Kim Young-woon (김영운), *Gugak gaeron*
(국악개론, 2015); Keith Howard, *Perspectives on Korean Music* (Ashgate, 2006).

## In the MIDI clip's "Mode or raga..." list

The five Chinese modes, the Japanese scales and the Korean modes above are also in the MIDI clip's list that moves
every note to the nearest note of a mode (`midifile::scales()`), under "Chinese", "Japanese" and "Korean".
