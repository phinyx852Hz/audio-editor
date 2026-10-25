# Instruments

[]{.ix t="Instruments"}PHI comes with free samplers and synthesizers, built with PHI from their own source and named by what they do. The Sample menu has the samplers and the Synth menu (`Alt+Y`) the synths; the Find panel lists them all under **Instruments**.

![An instrument in its own floating window, with PHI's opacity bar along the bottom.](images/instrument-window.png)

## The instruments

| Name in PHI | Instrument | Kind |
|---|---|---|
| Granular | GRNLR | A granular sampler: clouds of tiny grains from a sound |
| Retro Sampler | Ami Sampler | A sampler in the style of the Amiga, with 12 slots |
| Wavetable | Vaporizer2 | A wavetable synthesizer |
| Modular | Cardinal | A modular synthesizer, with VCV Fundamental's oscillators, filter, amplifiers, envelope, LFOs, noise, mixers, sequencer and scope |
| Additive | Overtonium | An additive synthesizer, building tones from overtones |
| Subtractive1 mono | CA-72 | A monophonic synthesizer after the Minimoog Model D |
| Subtractive2 poly | Helm | A polyphonic subtractive synthesizer |
| FM | OctaSine | A frequency-modulation synthesizer |

Each opens in its own floating window, with its own look and fonts, untouched. A thin **opacity bar** runs along the bottom of each window: drag or scroll it, and double-click it for solid. Each instrument remembers its own opacity.

::: note
OctaSine draws with OpenGL 3.2 and 4× multisampling, which any graphics driver has but some virtual machines lack. CA-72 and OctaSine are only built when Rust is installed.
:::

## Playing an instrument

[]{.ix t="Instruments!playing"}An instrument opened from the menu plays as one of the patchbay's four synths, so:

- the **MIDI keyboard** plays it (Sample > MIDI Inputs chooses the keyboard);
- the **Patchbay** (`F4`) routes it anywhere (see [The Patchbay](#patchbay));
- the **mixer's tracks** can use it too, as their instrument.

## On mixer tracks {#mixer-instruments}

Any instrument, PHI's own or any VST3, LV2 or CLAP instrument on your system, can be a mixer track's instrument, playing the track's MIDI clips. See [The Mixer, Patchbay and Projects](#mixer).

## The Retro Sampler and the tracker

[]{.ix t="Retro Sampler"}The Retro Sampler can play the notes of Tracker1's instruments: each of its 12 slots listens on its own MIDI channel. See [Tracker1 and the Retro Sampler](#tracker-ami).

## The Φ synth

A phase-distortion synthesizer of PHI's own is being built. See [Φ the synth](#phi-synth).
