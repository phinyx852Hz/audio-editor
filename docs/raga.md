# PHI rāga

A Hindustani raga instrument (owner, 2026-10-25; the plan is the "PHI rāga" doc shared with the
owner and their sitar teacher). Plain C++ in phi_dsp, so the PHI synth can use it too:

- `Source/core/PhiRaga.h`: the raga sheets and the raga player.
- `Source/core/PhiRagas.cpp`: the sheets PHI comes with (44 ragas, the owner's list).
- `Source/core/PhiSitar.h`: the sitar voice.
- `Source/mixer/RagaInstrument.h`: PHI rāga as an instrument on a mixer track.

## Raga sheets

One plain-text sheet per raga, which the owner and the teacher edit without code. A copy for
them is in the project files (`tunings/raga-sheets.txt`).

```
raga: darbārī kānaḍā
plain: darbari kanada
thaat: āsāvarī
svaras: S R g=32/27 m P d=128/81 n
aroha: n, S R g m P d n S'
avaroha: S' d n P m P g m R S
vadi: R
samvadi: P
pakad: g m R S | d n P
andolan: g 20 0.5, d 20 0.5
kan: m>g
time: midnight
```

- **Svaras:** S r R g G m M P d D n N. Lower case is komal and M is tīvra ma.
- **Octaves:** "," marks the lower octave and "'" the upper. A dot below or above also works.
- **Tuning:** a ratio (`g=32/27`) or cents (`g=294c`). A svara without one is in five-limit just
  intonation.
- **Āndolan:** the svara, then the depth in cents, then the rate in Hz.
- **Mistakes:** each one is reported by its line number, and the sheet is left out.

The sheets' āroha, avaroha, vādī, samvādī, pakaḍ and āndolan are a first draft for the teacher to
correct. The mixer's raga tunings (`Tunings.h`) come from the sheets: each raga in its own tuning,
and again equal tempered.

## The raga player

`phi::RagaPlayer` turns keys into svaras:

- **Direction:** a key goes to the svara the raga uses in the direction the melody moves, from its
  āroha going up and its avaroha coming down. The same key therefore plays N going up and n coming
  down in khamāj.
- **Keys outside the raga:** they go to the nearest svara, with ties going the way the melody moves.
- **Tuning and āndolan:** each svara is tuned as its sheet says. A svara with āndolan swings below
  its pitch and back while held. Ornaments go from 0 (plain svaras) to 1 (as the sheet says).
- **Mīṇḍ:** `glide` pulls a sounding string to a new svara, touching each of the raga's svaras on
  the way (those it uses in that direction). On a track, a key pressed while another is held plays
  mīṇḍ (legato), over the window's mīṇḍ time.
- **Kaṇ:** a sheet's `kan: m>g` touches ma for an instant before ga, then ga and its āndolan.
- **Struck ornaments:** a sheet lists the svaras struck with each, as `khatka: G`, `murki: G, n`,
  `krintan: P` or `zamzama: R`. Each is a quick path into the svara through its neighbours in the
  raga, then the svara held:
  - khaṭkā: above, the svara, below, the svara (forceful, 45 ms a step);
  - murkī: the svara, above, the svara, below, the svara (light, 30 ms a step);
  - krintan: two above, one above, the svara, each a pull-off onto its fret;
  - zamzama: above, the svara, above, the svara, below, the svara, hammered and pulled.

  A svara with more than one takes the first of khaṭkā, murkī, krintan, zamzama, kaṇ. Mīṇḍ
  (legato) replaces them.
- **Gamak:** `gamak: d 5` shakes komal dha up to the next svara of the raga and back five times a
  second while it is held, in place of āndolan.

## The sitar

`phi::Sitar` has four played strings, 13 tarab and two chikārī:

- **Strings:** each is a waveguide: a delay line one period long with its losses in the loop. Its
  length follows the pitch, so bends and āndolan pull the string.
- **Jawārī:** the string is shortened as it presses the bridge, which gives the buzz. It runs from
  0 (open) to 1 (closed).
- **Tarab:** the sympathetic strings are fed a little of what is played, and only those tuned to
  it build up.
- **Gourd:** two resonances.
- **Cost:** four strings and 13 tarab render 5 s in about 0.2 to 0.3 s on one core.

## On a mixer track

PHI rāga is a built-in instrument, alongside Synth, Sampler and Slicer.

- **Which raga plays:** the one in force for the track when the Inspector's tuning (clip, else
  scene, else track) is a raga, using that tuning's root as Sa. Otherwise it plays the
  instrument's own raga.
- **Its window (Edit):**
  - Raga and Sa
  - Ornaments
  - Jawārī
  - Tarab level
- **Chikārī:** keys two octaves and more below Sa strike it.
- **Pitch bend:** ±2 semitones.
- **Saving:** a mix saves it as `{"kind": "raga", "state": "raga=…;sa=…;…"}`.

## Next

- Khaṭkā, murkī, gamak, krintan and zamzama.
- Avoided moves.
- MPE in and out.
- The tānpūrā.
- Up to 100 ragas from the teacher's sheets.
- Editing the sheets inside PHI.
