# On Φ

::: epigraph
*Geometry has two great treasures: one is the theorem of Pythagoras; the other, the division of a line into extreme and mean ratio.*

Johannes Kepler
:::

PHI is named after a letter, and the letter is named after a number. This chapter is about that number, the golden ratio, and about the other things the Greek letter Φ stands for: an angle, a phase, a magnetic flux. It explains where each of them is at work inside PHI, what each one does to the sound, and where the line runs between making a sound better and making a new sound.

You do not need any of this to use PHI. But if you have wondered why a Φ button sits next to a stereo widener, or why an echo seems to wander instead of repeat, this is the chapter that answers it.

## A number that never lines up {#phi-intro}

[]{.ix t="Golden ratio"}[]{.ix t="φ (golden ratio)"}Take a line and cut it in two, so that the whole line is to the longer piece as the longer piece is to the shorter. There is exactly one place to cut. The ratio you get is the golden ratio, written φ (phi):

::: formula
φ = (1 + √5) ÷ 2 = 1.618 033 988 749 894…
:::

It has two habits that no other number has. Squaring it adds one, and dividing one by it takes one away:

::: formula
φ² = φ + 1 = 2.618…    1/φ = φ − 1 = 0.618…
:::

So the golden ratio repeats itself at every scale. Cut a square off a golden rectangle and what is left is a smaller golden rectangle; cut a square off that, and so on for ever. Draw a quarter circle through each square and you get the golden spiral.

![The golden rectangle and its spiral. Every rectangle left over is golden again.](images/phi/golden-rectangle.svg)

[]{.ix t="Fibonacci numbers"}The same number hides in the Fibonacci numbers, where each is the sum of the two before it: 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610… Divide any one by the one before it and the answer creeps towards φ: 8/5 = 1.6, 13/8 = 1.625, 21/13 = 1.615, 233/144 = 1.618 05. PHI's defaults borrow these numbers too: 13 ms of pre-delay, a 233 ms echo, 61.8 % and 38.2 % mixes.

Here is the property that matters most for sound. Every number can be approached by fractions, and most numbers are approached quickly: π is almost exactly 355/113. The golden ratio is approached more slowly than any other number. Its best fractions are the Fibonacci ratios, and each one is only a little better than the last. Mathematicians call φ the *most irrational number*.

In sound, a simple fraction means things that line up. Two delays of 300 and 450 ms meet again every 900 ms; two tones at 200 and 300 Hz share a beat every hundredth of a second; an LFO at 1 Hz and one at 2 Hz come back to where they started every second. When lengths, frequencies or rates are a golden ratio apart, they never meet again. Almost everything PHI does with φ comes from this one fact.

::: phi
**The theme of this chapter:** φ is the number that never lines up. Use it where things lining up sounds bad (reverb, widening, sampling the sphere), and where never repeating sounds alive (motion, rhythm, randomness).
:::

## Capital Φ, small φ and ϕ {#phi-letters}

[]{.ix t="Φ (letter)"}The Greek letter phi has a capital, Φ, and a small form drawn two ways, φ (looped) and ϕ (straight-stemmed). Different fields use them for different things, and the same field is not always consistent.

| Symbol | Usually means | In PHI |
|---|---|---|
| Φ | The golden ratio (in some books), magnetic flux, a flux in general | The name of the program, its mark and its golden-ratio features |
| φ | The golden ratio 1.618 | Every golden-ratio number in PHI's code and in this manual |
| 1/φ, sometimes written φ′ or Φ | The golden ratio's reciprocal, 0.618 | Decays, mixes and densities (61.8 %) |
| ϕ or φ | A phase angle, in radians or degrees | The phase of oscillators, LFOs and the stereo field |

This manual uses φ for 1.618… throughout and Φ for the program and its features. Where a phase angle is meant, it says so.

## The golden angle, and φ as a phase {#golden-angle}

[]{.ix t="Golden angle"}[]{.ix t="Phase!golden angle"}Divide a full circle in the golden ratio and the smaller part is the golden angle:

::: formula
360° ÷ φ² = 137.507 764…°  =  π (3 − √5) radians = 2.399 963… rad
:::

![The golden angle splits the circle into two parts in the golden ratio.](images/phi/golden-angle.svg)

Turn by the golden angle, then again, then again, and you never land where you have been. Each new turn falls into the largest gap the earlier ones left. A sunflower places its seeds this way, each one 137.5° round from the last, so they pack its head evenly with no gaps and no rows; pine cones, pineapples and the leaves around a stem do the same. You can count the spirals: 34 one way and 55 the other, or 55 and 89. Fibonacci numbers again.

![Six hundred and ten points, each turned 137.5° from the last: the sunflower pattern.](images/phi/sunflower.svg)

### Phase in audio

[]{.ix t="Phase"}A phase angle (often written ϕ) says where in its cycle a wave is: 0° at the start, 180° halfway, 360° back at the start, or 0 to 2π in radians. Phase is everywhere in audio. Two copies of a sound 180° apart cancel; 90° apart, they widen. The stereo field is itself an angle: the goniometer draws mid upwards and side across, and a sound panned hard left sits at 45° from the middle. PHI shows phase in several places:

- The **correlation meter** reads from +1 (left and right in phase, mono-safe) through 0 to −1 (in opposite phase, vanishing in mono). See [Meters and Analysis](#meters).
- The **Phase Wheel** in the Analysis Meters window shows each frequency's phase between mono, left, right and inverse.
- **Mid/Side Width** has a **Rotation** control (±45°) that turns the whole stereo image like a dial.
- The **Polar** views of the imaging display mark the 45° lines; inside them is mono-safe.

### Where PHI turns by the golden angle

When PHI needs to place several things around a circle so that none of them crowd together, however many there are, it turns by the golden angle.

- **Φ Echo** pans its taps around the stereo field a golden angle apart, so each echo comes from somewhere new.
- **Φ Drift** sets the left and right sides of its chorus a golden angle apart in phase, so the two sides move together but never in step.
- **Sonify**'s Spiral mode reads a picture along a sunflower spiral, one golden-angle turn per step, from the centre outwards, so a still picture plays as a melody that never repeats.
- **Ambisonics**: to measure or shape a sound field PHI needs points spread evenly over a sphere. It places them by the golden angle down a spiral from the north pole to the south (a Fibonacci sphere). The 240 virtual speakers behind every AllRAD speaker decoder, the virtual layout heard through the HRTF when you decode to headphones, the fit behind Rotate / Mirror Scene and the slice of sphere the Spatial Slicer cuts are all built on such points. Even points mean no direction counts more than another.

![Points spread over a sphere by the golden angle, as PHI places them to measure a sound field.](images/phi/fibonacci-sphere.svg)

## Φ in time {#phi-time}

### Rhythms that never repeat

[]{.ix t="Fibonacci word"}[]{.ix t="Φ Rhythm"}Write 1, then 10, then join each word to the one before it: 1, 10, 101, 10110, 10110101, 1011010110110… This is the Fibonacci word. Its ones and zeros never fall into a repeating loop, yet they never clump either: the ones always make up the same share of the steps, 61.8 % of them, and never two zeros come together. Mathematicians call such sequences *Sturmian*, the most evenly spread a two-letter pattern can be without repeating.

As a rhythm it feels like a groove that keeps nearly repeating and then shifts. **Φ Rhythm** is a gate that opens on the hits of such a pattern, at four densities: 61.8 %, 38.2 %, 23.6 % or 14.6 % of the steps (1/φ, 1/φ², 1/φ³, 1/φ⁴). Shift rotates the pattern, Depth sets how far a closed step is turned down, and Smooth softens the edges so it never clicks.

![Φ Rhythm's four densities over two bars of sixteenths.](images/phi/rhythm.svg)

Sonify uses the same word for its drums: with **Φ rhythm** on, a drum only strikes on the ones.

### Motion that never repeats

[]{.ix t="Φ Drift"}[]{.ix t="Quasi-periodic motion"}An LFO repeats: a chorus or tremolo at 1 Hz sounds exactly the same every second, and the ear soon hears the loop. Add a second LFO at 2 Hz and the pair still repeats every second. Add one at 1.618 Hz instead and the pair never comes back to the same shape. **Φ Drift** sums three LFOs at the rate, the rate × φ and the rate × φ² (0.618, 1 and 1.618 Hz by default). The result is motion that is smooth and steady but never loops. It drives a chorus, a tremolo or an auto-pan.

## Φ in frequency {#phi-frequency}

### Golden steps across the spectrum

[]{.ix t="Φ EQ"}A step of φ in frequency is 12 × log₂ φ = 8.33 semitones, or 0.694 of an octave: a little wider than a minor sixth. **Φ EQ** places its bands at golden steps: a Super Low shelf at 80 Hz, ten bells at 80 Hz × φ, φ², … φ¹⁰ (129 Hz up to 9.8 kHz), and a Super High shelf at 80 Hz × φ¹¹ = 15.9 kHz. Each bell is exactly as wide as the step between its neighbours (a Q of φ^1.5 = 2.06), so the bands meet without gaps or pile-ups: a graphic EQ with slightly fewer, slightly wider bands than a two-thirds-octave one.

![Φ EQ's twelve bands, each a golden step from the next.](images/phi/phi-eq.svg)

### Between the keys

[]{.ix t="Φ scale"}[]{.ix t="Φ Stack"}[]{.ix t="Φ Resonator"}Because φ is irrational, none of its powers ever lands on a note of the piano. Fold φ⁰ to φ⁷ into one octave and you get eight pitches that fall between the keys, each 833 cents round from the last.

![The first eight powers of φ, folded into one octave.](images/phi/phi-octave.svg)

PHI plays with this in three places.

- **Φ Stack** adds copies of the sound pitched up by φ, φ², φ³ and φ⁴, each 1/φ quieter, fanned out round the original. The copies do not form a chord in any key, yet they seem to belong together: bells, glass and drifting pads.
- **Φ Resonator** picks narrow bands out of a sound at a root × φⁿ (with a root of 220 Hz: 220, 356, 576, 932 and 1507 Hz), the root in the middle and the others fanned left, right, left. Turn up Noise and it becomes a sound source of its own, white noise ringing at golden frequencies.
- **Sonify** offers a **Φ scale**: the powers of φ folded into the octave, as the notes a picture is played on.

### The golden EQ

[]{.ix t="Φ Tame"}**Φ Tame** follows a rule of thumb from a tutorial on golden-ratio sound design: find the fundamental of a drum loop or bass sound (say 58.9 Hz), multiply it by φ ÷ 2 × 10 (that is, by 8.09), and cut gently there (476.5 Hz). The cut lands in the boxy lower mids, about three octaves above the fundamental, and softens drums much like gentle compression without touching their dynamics. Φ Tame finds the fundamental by itself between 25 and 250 Hz and follows it as it changes, or you can set it by hand.

::: note
Φ Tame's frequency is a rule of thumb, not a law of acoustics. It works because it reliably lands in the region where many drums and loops build up, and you should trust your ears over the number.
:::

## Φ in space {#phi-space}

### Echoes that never stack

[]{.ix t="Φ Echo"}A plain delay with feedback repeats at the same interval, and its repeats land on the beat grid it was set to. **Φ Echo** instead places its taps at the time × φ⁰, φ¹, φ², φ³…: with a first tap at 250 ms, the others come at 405, 655, 1059 and 1713 ms. Each is 1/φ quieter than the one before (by default), and each is panned a golden angle round from the last. The echoes spread out like a ripple instead of marching, and because no two taps are a simple ratio apart, they never fall on top of one another.

![Φ Echo's taps: φ later and 1/φ quieter each time, and each turned by the golden angle.](images/phi/echo-taps.svg)

### A reverb without ringing

[]{.ix t="Φ Space"}A digital reverb is made of delay lines feeding into each other. If two of their lengths share a common period, they reinforce each other at that period and the tail rings or flutters. Reverb designers have long chosen lengths with no common factors to avoid it. **Φ Space** uses eight lines whose lengths step by √φ (1.272), so no two lengths share a period, and the tail comes out smooth. Size sets the shortest line, from 7 to 34 ms (Fibonacci numbers again); Decay, Damping, Pre-delay and Mix do what they say.

### Widening without combing

[]{.ix t="Stereoize!Φ golden ratio switch"}[]{.ix t="Mid/Side Width!generate sides (Φ)"}The classic way to make mono into stereo is to add a delayed copy to one side and take it from the other. It works, but it colours the sound with a comb: notches evenly spaced up the spectrum, which can sound pitched or phasey. **Stereoize**'s Φ switch replaces the single delay with four allpass filters at the delay, 1/φ, 1/φ² and 1/φ³ of it, each with a feedback of 1/φ. Their notches never line up, so the colour spreads into something closer to a natural room. The sides still cancel exactly when you fold to mono, so nothing is lost. **Mid/Side Width**'s Generate sides has the same switch.

## Φ in choice {#phi-choice}

[]{.ix t="Golden sequence"}[]{.ix t="Low-discrepancy sequence"}Sometimes PHI has to choose something at random: a patch, a sound, a combination of samples. True random numbers clump: press a random button ten times and you will often get two results that sound almost the same. PHI uses the golden sequence instead. Keep adding 1/φ and keep the part after the decimal point: 0.618, 0.236, 0.854, 0.472, 0.090, 0.708… Each new value lands in the largest gap the earlier ones left. It never repeats, and a short run of it covers the range far more evenly than random numbers do. This is a *low-discrepancy sequence*.

- The **Φ random selector** deals one of each Selector colour's favourite sounds onto your tracks. Each colour walks its favourites by its own golden-type step, so the colours never fall into step with each other and every press gives a new combination.
- The **Φ splash** plays a new sound each time PHI starts, chosen the same way.
- The **Φ synth**'s Φ button makes a new patch with every press (see [Φ the synth](#phi-synth)).

For many values at once (a patch has dozens of settings) PHI uses the generalisation by Martin Roberts: in *d* dimensions, the step is built from the root of x^(d+1) = x + 1, which for one dimension is φ itself. Every new point lands in the biggest gap across all the settings together.

## Magnetic flux and the other Φs {#phi-flux}

[]{.ix t="Magnetic flux"}[]{.ix t="Flux"}In physics, Φ most often means flux: how much of a field passes through a surface. Magnetic flux, Φ_B, measured in webers, is the one that matters for sound, because for most of the twentieth century sound *was* magnetic flux.

- **Microphones, pickups and speakers.** A dynamic microphone or a guitar pickup makes a voltage because the magnetic flux through its coil changes (Faraday's law: the voltage is −dΦ/dt). A loudspeaker runs the same law backwards.
- **Tape.** A tape recorder stores sound as a pattern of magnetic flux along the tape. Studios calibrated their machines to a reference fluxivity in nanowebers per metre: 185 nWb/m, later 250, 320 and 355 nWb/m for hotter tapes. The meters you see in every DAW, PHI's included, still carry the habits of that era: 0 VU, headroom, a reference level below full scale.
- **The golden ratio in a magnet.** In 2010 physicists cooling a chain of cobalt niobate, a magnetic crystal, found that the frequencies of its two lowest resonances stood in the golden ratio, as theory had predicted for that kind of quantum chain. It is a rare place where φ and magnetic flux meet.

PHI does not simulate magnetic flux itself. It does not model a tape head or a coil. Where you want that sound, it comes from plugins: Airwindows Consolidated, bundled with PHI, includes several tape and console simulations, and Varispeed in the Warp menu changes speed and pitch together, just as tape does.

Other fields use the letter too: φ for electric potential, Φ_v for luminous flux in lighting, φ for the work function of a metal, ϕ for the angle of a polar coordinate. Of these, only the polar angle shows up in PHI: the azimuth of a sound in the Binaural and Ambisonics tools is an angle round the listener.

## Better sound, or new sound? {#phi-quality}

[]{.ix t="Golden ratio!and sound quality"}It is easy to find claims that the golden ratio makes sound purer, more natural or more healing. Most of them do not survive a careful listen. There is no golden frequency, no golden tuning that makes music better in itself, and nothing in hearing tuned to 1.618.

What the golden ratio really offers is the one property this chapter began with: it never lines up. That makes it genuinely useful wherever lining up is the problem, and genuinely interesting wherever never repeating is the point. PHI's Φ features fall into three groups.

| Feature | What φ does | Purpose |
|---|---|---|
| Φ Space | Delay lengths with no common period: a smoother tail | Better sound |
| Stereoize and Mid/Side Width, Φ switch | Allpasses whose notches never line up: less comb colour, still mono-safe | Better sound |
| Ambisonics (Fibonacci sphere) | Even sampling of every direction: fairer decoders and meters | Better sound |
| Φ De-resonator | Compares each frequency with its neighbours over one golden step; cuts recover φ times slower than they close | Better sound |
| Golden sequence (Φ random selector, splash, Φ button) | Choices spread evenly, never repeating | Better choices |
| Φ EQ | Bands a golden step apart, each as wide as the step | A tool with its own spacing |
| Φ Tame | A cut at the fundamental × 8.09 | A rule of thumb that often helps |
| Φ Echo | Taps that never meet, panned by the golden angle | Character |
| Φ Drift | Motion that never loops | Character |
| Φ Rhythm, Sonify's Φ rhythm | A rhythm that never repeats and never clumps | Experiment |
| Φ Stack, Φ Resonator, Sonify's Φ scale | Pitches between the keys that still belong together | Experiment |

::: tip
The quality features are safe to leave on: the Φ switches in Stereoize and Mid/Side Width make the widening less coloured without changing anything else. The experimental ones are instruments to play with: put Φ Stack or Φ Resonator on a send, or record Φ Rhythm onto a pad, and listen for what it suggests.
:::

## The Φ effects at a glance {#phi-effects}

[]{.ix t="Φ effects"}All the Φ effects are under **Built-in** in *Add plugin* (`Ctrl+Alt+E`), and in the Find panel under Effects. They exist only in PHI. Each opens in a black window with the gold Φ mark, and docks as a card in the Device Chain like any other plugin. See [The Master Section and Device Chain](#master-section) for how to add, edit and reorder them.

| Effect | Controls (defaults) | In short |
|---|---|---|
| Φ EQ | Super Low, ten bands, Super High (±dB), Output | Twelve golden-step bands from 80 Hz to 15.9 kHz |
| Φ Echo | Time (250 ms), Decay (1/φ), Feedback, Mix, Taps (5), Spread | Taps at time × φⁿ, panned by the golden angle |
| Φ Space | Size, Decay (2 s), Damping, Mix, Pre-delay (13 ms) | Eight lines √φ apart: a smooth reverb |
| Φ Drift | Mode (Chorus, Tremolo, Pan), Rate (0.618 Hz), Depth, Mix | Three LFOs at rate × 1, φ, φ² |
| Φ Rhythm | Tempo, Density (61.8 % to 14.6 %), Shift, Depth, Step, Smooth | A gate playing the Fibonacci word |
| Φ Resonator | Root (220 Hz), Bands (5), Sharpness (Q 30), Mix, Width, Noise, Output | Narrow bands at root × φⁿ |
| Φ Stack | Copies (4), Decay, Tone (8 kHz), Mix, Width | Copies pitched up by φ, φ², φ³, φ⁴ |
| Φ Tame | Depth, Fundamental (55 Hz), Width (Q 1.618), Find Fundamental | A cut at the fundamental × φ ÷ 2 × 10 |
| Φ De-resonator | Depth, Sharpness, Speed, Mix, Low, High, Delta | Turns resonances down while they ring |

Φ De-resonator delays the sound by 2048 samples (43 ms at 48 kHz) so it can look at each frequency before it plays; PHI compensates for that delay automatically. **Delta** plays only what it takes away, which is the quickest way to hear what it is doing.

## The Φ mark {#phi-mark}

[]{.ix t="Φ mark"}PHI's logo is its own capital phi, drawn from golden-ratio geometry rather than taken from a font. In units where the mark is 1000 tall:

| Part | Size | Why |
|---|---|---|
| Height | 1000 | |
| Ring width | 618 = 1000 ÷ φ | Ring and stem are in golden proportion |
| Ring height | 382 = 618 ÷ φ | The ring is a golden ellipse; the stem is φ² times its height |
| Stem width | 90.2 = 1000 ÷ φ⁵ | |
| Ring stroke | One stem wide at the sides, 1/φ of a stem at top and bottom | The thick and thin of a pen stroke |
| Serifs | φ² stems wide, 1/φ² of a stem tall | |

The mark is gold, `#c8a24a`, on black, `#0b0b0c`. PHI draws it itself from these numbers, so it is sharp at every size. A compact form, as tall as the ring is wide and without serifs, is the **Φ button** you meet across the program: in Stereoize, in Mid/Side Width, in Sonify and at the end of the Selector. Lit, it is on; faded to 1/φ² of its brightness, it is off.

The splash that greets you as PHI starts is the gold mark on its black tile, 382 pixels square (1000 ÷ φ²), and it stays for at least 1.618 seconds.

## Φ the synth {#phi-synth}

[]{.ix t="Φ synth"}[]{.ix t="Phase distortion"}Φ also names an instrument: a phase-distortion synthesizer being built for PHI. It brings this chapter full circle, because phase distortion is synthesis by bending the phase angle φ itself.

An ordinary oscillator reads a cosine at a steady speed: its phase runs smoothly from 0 to 360° once per cycle, and out comes a pure, round tone. Phase distortion lets the phase run unevenly, racing through part of the cycle and dawdling through the rest. The cosine is read through that bent phase, and the more the phase is bent, the brighter the wave: towards a sawtooth, a square, a pulse, or a resonant sweep. Sweep the bend with an envelope and the tone opens and closes like a filter, with no filter at all. This was the sound of Casio's CZ synthesizers in the 1980s.

![Phase distortion: the same cosine read through a straight phase (left) and a bent one (right).](images/phi/phase-distortion.svg)

The Φ synth builds on that with golden-ratio ideas of its own:

- a **Φ button** that makes a new patch with every press, from the golden sequence across all its settings, so it never gives the same patch twice;
- **φ detune**, spreading voices by golden ratios so they shimmer without beating in step;
- **quasi-periodic LFOs**, moving at golden ratios to each other like Φ Drift;
- **Fibonacci rhythms**, and an **XY pad** for playing several settings at once.

::: note
The Φ synth is still being built, and this section will grow into its own chapter when it joins PHI. Until then, the sound you hear as PHI starts is a small phase-distortion voice written for the splash: each start plays a new patch, chosen by the golden sequence, through a short granular reverb.
:::

::: epigraph
*Nature uses as little as possible of anything.*

Johannes Kepler
:::
